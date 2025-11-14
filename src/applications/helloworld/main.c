#include "autoconf.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <getopt.h>
#include <stdlib.h>

#include <rtthread.h>

#include "terminal.h"

#include "lvgl.h"
#include "lv_rt_thread_port.h"
#include "terminal_lvgl.h"

static const char * BUFFER_KEY = "buffer key";
static volatile bool is_running = false;
static int pipefds[2];

static void handle_signals(int sig, siginfo_t *info, void *context) {
    switch(sig) {
        case SIGINT:
        case SIGQUIT:
        case SIGTERM:
            is_running = false;
            break;
        default:
            break;
    }
}

static void print_usage(const char *program_name) {
    printf("Usage: %s Path [Options]\n", program_name);
    printf("Arguments:\n");
    printf("  Path                 Serial device file path (e.g., /dev/ttyUSB0)\n");
    printf("Options:\n");
    printf("  -b, --baud BAUD      Baud rate (default: 115200)\n");
    printf("  -d, --databits BITS  Data bits (5, 6, 7, 8) (default: 8)\n");
    printf("  -p, --parity PARITY  Parity bit (n: none, e: even, o: odd) (default: n)\n");
    printf("  -s, --stopbits BITS  Stop bits (1, 2) (default: 1)\n");
    printf("  -h, --help           Show this help message\n");
}

int main(int argc, char *argv[]) {
    if (pipe(pipefds) == -1) {
        perror("pipe");
        return -1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handle_signals;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        close(pipefds[0]);
        close(pipefds[1]);
        return -1;
    }

    char *tty_path = NULL;
    int option_baud_rate = 115200;
    int option_data_bits = 8;
    char option_parity = 'n';
    int option_stop_bits = 1;
    
    static struct option long_options[] = {
        {"baud", required_argument, 0, 'b'},
        {"databits", required_argument, 0, 'd'},
        {"parity", required_argument, 0, 'p'},
        {"stopbits", required_argument, 0, 's'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    if (argc < 2) {
        print_usage(argv[0]);
        close(pipefds[0]);
        close(pipefds[1]);
        return -EINVAL;
    }
    
    tty_path = argv[1];
    
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc - 1, argv + 1, "b:d:p:s:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'b':
                option_baud_rate = atoi(optarg);
                break;
            case 'd':
                option_data_bits = atoi(optarg);
                break;
            case 'p':
                option_parity = optarg[0];
                break;
            case 's':
                option_stop_bits = atoi(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    uint32_t baud_rate = option_baud_rate;

    uint8_t data_bits = option_data_bits;

    uint8_t parity = 0;
    switch (option_parity) {
        case 'n': // 无校验
            parity = 0;
            break;
        case 'o': // 奇校验
            parity = 1;
            break;
        case 'e': // 偶校验
            parity = 2;
            break;
        // case 'm': // 标志校验
        //     parity = 3;
        //     break;
        // case 's': // 空格校验
        //     parity = 4;
        //     break;
        default:
            fprintf(stderr, "Unsupported parity: %c\n", option_parity);
            close(pipefds[0]);
            close(pipefds[1]);
            return -EINVAL;
    }

    uint8_t stop_bits = 0;
    if (option_stop_bits == 1) {
        stop_bits = 0;
    } else if (option_stop_bits == 2) {
        stop_bits = 2;
    } else {
        fprintf(stderr, "Unsupported stop bits: %d\n", option_stop_bits);
        close(pipefds[0]);
        close(pipefds[1]);
        return -EINVAL;
    }

    terminal_tty_config_t terminal_tty_config = {
        .tty_path = tty_path,
        .baud_rate = baud_rate,
        .data_bits = data_bits,
        .parity = parity,
        .stop_bits = stop_bits,
        .pip_fd = pipefds[0],
    };

    printf("main: tty_path = %s\n", tty_path);
    printf("main: baud_rate = %d\n", baud_rate);
    printf("main: data_bits = %d\n", data_bits);
    printf("main: parity = %d\n", parity);
    printf("main: stop_bits = %d\n", stop_bits);

    int err = lvgl_thread_init(8 * 1024, 20, terminal_gui_init, terminal_gui_deinit, &terminal_tty_config);
    if (err) {
        printf("lvgl_thread_init failed!\n");
        return err;
    }

    is_running = true;

    while (is_running)
    {
      rt_thread_mdelay(500);
    }

    int pipe_notify = 0;
    write(pipefds[1], &pipe_notify, sizeof(int));

    err = lvgl_thread_deint();

    close(pipefds[0]);
    close(pipefds[1]);

    return err;
}
