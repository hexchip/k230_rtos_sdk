#include "terminal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>


#include "tmt.h"

#define TERMINAL_POLL_RETRY_LIMIT 3

typedef struct terminal_context {
    TMT *vt;
    char line_buffer[256];
    size_t line_buffer_size;
    volatile bool is_thread_runing;
    int pip_fd;
    int tty_fd;
    bool is_tty_hangup;
    pthread_t thread;
} terminal_context_t;

#define UART_IOCTL_SET_CONFIG   _IOW('U', 0, void*)
#define UART_IOCTL_GET_CONFIG   _IOR('U', 1, void*)

struct serial_configure {
    uint32_t baud_rate;

    uint32_t data_bits               :4;
    uint32_t stop_bits               :2;
    uint32_t parity                  :2;
    uint32_t bit_order               :1;
    uint32_t invert                  :1;
    uint32_t bufsz                   :16;
    uint32_t reserved                :6;
};

static size_t terminal_stream_memcpy(char *dest, const char *src, size_t size) {
    size_t residual_size = size;
    size_t total_size = 0;
    while (residual_size) {
        if (*src == '\n') {
            *dest = '\r';
            ++ dest;
            ++ total_size;
        }

        *dest = *src;
        ++ dest;

        if (*src == '\r') {
            *dest = '\n';
            ++ dest;
            ++ total_size;
        }

        ++ src;
        -- residual_size;
        ++ total_size;
    }

    return total_size;
}

static void tmt_callback(tmt_msg_t m, TMT *vt, const void *a, void *context) {
    terminal_t *terminal = context;
    switch (m){
        case TMT_MSG_BELL:
            /* the terminal is requesting that we ring the bell/flash the
             * screen/do whatever ^G is supposed to do; a is NULL
             */
            // TODO At present, we have no sound output.
            break;

        case TMT_MSG_UPDATE:
            const TMTSCREEN *screen = a;
            char *buffer = terminal->context->line_buffer;
            size_t buffer_size = terminal->context->line_buffer_size;
            size_t dirty_line_index_array[256];
            size_t dirty_line_count = 0;
            for (size_t r = 0; r < screen->nline; r++) {
                if (screen->lines[r]->dirty) {
                    dirty_line_index_array[dirty_line_count] = r;
                    dirty_line_count++;
                    for (size_t col = 0; col < screen->ncol; col++) {
                          // Convert wide characters to multibyte characters.
                          wchar_t wc = screen->lines[r]->chars[col].c;
                          if (wc == L'\0' || wc == TMT_INVALID_CHAR) {
                              buffer[col] = ' ';
                          } else {
                              // Simplified processing: Assume it is ASCII characters.
                              buffer[col] = (char)wc;
                          }
                    }
                    buffer[screen->ncol] = '\0';
                    terminal->gui_config.lifecycle->on_line_update(r, buffer, buffer_size);
                }
            }
            
            if (dirty_line_count > 0) {
                terminal->gui_config.lifecycle->on_refresh_dirty_lines(dirty_line_index_array, dirty_line_count);
            }

            // Mark all dirty line as cleared.
            tmt_clean(vt);
            break;

        case TMT_MSG_ANSWER:
            /* the terminal has a response to give to the program; a is a
             * pointer to a string */
            // TODO what this is used for?
            break;

        case TMT_MSG_MOVED:
            /* the cursor moved; a is a pointer to the cursor's TMTPOINT */
            const TMTPOINT *point = a;
            terminal->gui_config.lifecycle->on_cursor_move(point->r, point->c);
            break;
    }
}

static void msleep(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

static void* terminal_thread_entry(void *parameter) {
    terminal_t *terminal = parameter;
    terminal_context_t *context = terminal->context;

    context->is_thread_runing = true;

    int pip_fd = context->pip_fd;
    int tty_fd = context->tty_fd;

    TMT *vt = context->vt;

    struct pollfd pollset[2];
    pollset[0].fd = pip_fd;
    pollset[0].events = POLLIN;

    pollset[1].fd = tty_fd;
    pollset[1].events = POLLIN | POLLERR | POLLHUP;

    fd_set rfds, efds;
    char buffer[256];
    // char stream_buffer[384];
    uint8_t poll_retry_count = 0;
    while (context->is_thread_runing) {
        printf("%s poll start\n", terminal->name);
        int ret = poll(pollset, 2, -1);
        printf("%s poll end\n", terminal->name);

        if(ret > 0) {
            bool is_need_break = false;

            if (pollset[0].revents & POLLIN) {
                printf("%s user notify exit!\n", terminal->name);
                is_need_break = true;
            }

            if (pollset[1].revents & POLLHUP) {
                printf("%s pollhup\n", terminal->name);
                context->is_tty_hangup = true;
                is_need_break = true;
            }

            if (pollset[1].revents & POLLERR) {
                printf("%s pollerr\n", terminal->name);
                if (is_need_break) {
                    break;
                }
                else {
                    if (poll_retry_count > TERMINAL_POLL_RETRY_LIMIT) {
                        printf("%s exceeded the retry limit!\n", terminal->name);
                        break;
                    }
                    printf("%s poll retry count = %d\n", terminal->name, poll_retry_count);
                    msleep(1000 * poll_retry_count++);
                    printf("%s poll retry will continue loop\n", terminal->name);
                    continue;
                }
            }

            if (is_need_break) {
                printf("%s thread break!\n", terminal->name);
                break;
            }

            if (pollset[1].revents & POLLIN) {
                while (context->is_thread_runing) {
                    printf("terminal_thread_entry: start read \n");
                    ssize_t read_size = read(tty_fd, buffer, sizeof(buffer));
                    printf("terminal_thread_entry: read_size = %ld\n", read_size);
                    if (read_size == 0) {
                        break;
                    } else if (read_size < 0) {
                        perror("usb read failed");
                        break;
                    }
                    else {
                        // printf("tmt_write: %s |---\n", buffer, strlen(buffer));
                        // size_t stream_size = terminal_stream_memcpy(stream_buffer, buffer, read_size);
                        // printf("terminal_thread_entry: stream_size = %ld\n", stream_size);
                        // char log_buffer[384] = {0};
                        // // memcpy(log_buffer, stream_buffer, stream_size);
                        // memcpy(log_buffer, buffer, read_size);
                        // printf("tmt_write: %s |---\n", log_buffer);
                        // // tmt_write(vt, stream_buffer, stream_size);
                        tmt_write(vt, buffer, read_size);
                    }
                }
            }
        }
        else if (ret == 0) {
            // impossible
            printf("%s poll timeout\n", terminal->name);
            continue;
        }
        else {
            printf("%s poll failed: %s\n", terminal->name, strerror(errno));
            if (errno == EINTR) {
                break;
            }
            else {
                // TODO Is this even okay?
                continue;
            }
        }
    }

    tmt_write(vt, "a fatal error has occurred! please exit the application.\r\n", 0);
}

terminal_t * terminal_create(char *name, 
                                terminal_gui_config_t *gui_config, 
                                terminal_tty_config_t *tty_config,
                                size_t thread_stack_size, int thread_priority) {
    if(name == NULL) {
        errno = EINVAL;
        perror("terminal_create: name == NULL");
        return NULL;
    }

    if (gui_config->rows <= 0) {
        errno = EINVAL;
        perror("terminal_create: rows <= 0");
        return NULL;
    }

    if (gui_config->cols <= 0) {
        errno = EINVAL;
        perror("terminal_create: cols <= 0");
        return NULL;
    }

    if (gui_config->lifecycle == NULL) {
        errno = EINVAL;
        perror("terminal_create: lifecycle == NULL");
        return NULL;
    }
    
    if (tty_config->tty_path == NULL) {
        errno = EINVAL;
        perror("terminal_create: tty_path == NULL");
        return NULL;
    }

    // TODO validate tty_config

    terminal_t *terminal = malloc(sizeof(terminal_t));

    if (terminal == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    terminal->context = NULL;

    size_t name_buffer_size = sizeof(char) * (strlen(name) + 1);
    char *terminal_name = malloc(name_buffer_size);

    if (terminal_name == NULL) {
        free(terminal);
        errno = ENOMEM;
        return NULL;
    }

    memcpy(terminal_name, name, name_buffer_size);
    terminal->name = terminal_name;
    terminal->gui_config = *gui_config;
    terminal->tty_config = *tty_config;
    terminal->thread_stack_size = thread_stack_size;
    terminal->thread_priority = thread_priority;

    return terminal;
}

int terminal_init(terminal_t *terminal) {
    if (terminal->context != NULL) {
        return 0;
    }

    terminal->context = malloc(sizeof(terminal_context_t));
    if (terminal->context == NULL) {
        return -ENOMEM;
    }
    memset(terminal->context, 0, sizeof(struct terminal_context));

    terminal->context->pip_fd = terminal->tty_config.pip_fd;

    int tty_fd = open(terminal->tty_config.tty_path, O_RDONLY | O_NONBLOCK);
    if (tty_fd < 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Open %s error", terminal->tty_config.tty_path);
        perror(msg);
        return errno;
    }

    struct serial_configure serial_configure;
    int err = fcntl(tty_fd, UART_IOCTL_GET_CONFIG, &serial_configure);
    if (err) {
        perror("fcntl UART_IOCTL_GET_CONFIG");
        terminal_deinit(terminal);
        return err;
    }

    serial_configure.baud_rate = terminal->tty_config.baud_rate;
    serial_configure.data_bits = terminal->tty_config.data_bits;
    serial_configure.parity = terminal->tty_config.parity;
    serial_configure.stop_bits = terminal->tty_config.stop_bits;
    err = fcntl(tty_fd, UART_IOCTL_SET_CONFIG, &serial_configure);
    if (err) {
        perror("fcntl UART_IOCTL_SET_CONFIG");
        terminal_deinit(terminal);
        return err;
    }
    
    terminal->context->tty_fd = tty_fd;

    terminal->context->line_buffer_size = sizeof(char) * (terminal->gui_config.cols + 1);
    terminal->context->vt = tmt_open(terminal->gui_config.rows, terminal->gui_config.cols, tmt_callback, terminal, NULL);

    if (terminal->context->vt == NULL) {
        printf("Failed to initialize libtmt terminal.\n");
        terminal_deinit(terminal);
        return -ENOMEM;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    struct sched_param sched_param;
    sched_param.sched_priority = terminal->thread_priority;
    pthread_attr_setschedparam(&attr, &sched_param);
    pthread_attr_setstacksize(&attr, terminal->thread_stack_size);
    pthread_attr_setschedpolicy(&attr, SCHED_RR);

    pthread_create(&terminal->context->thread, &attr, terminal_thread_entry, terminal);
    if (terminal->context->thread == NULL) {
        printf("Failed to create terminal thread\n");
        terminal_deinit(terminal);
        return -ENOMEM;
    }

    return 0;
}

void terminal_deinit(terminal_t *terminal) {
    if (terminal->context == NULL) {
        return;
    }

    if (terminal->context->thread) {
        terminal->context->is_thread_runing = false;
        pthread_join(terminal->context->thread, NULL);
        terminal->context->thread = NULL;
    }

    if (terminal->context->vt) {
        tmt_close(terminal->context->vt);
    }

    if (terminal->context->tty_fd >= 0) {
        printf("terminal_deinit: close tty_fd start\n");
        close(terminal->context->tty_fd);
        printf("terminal_deinit: close tty_fd end\n");
    }

    free(terminal->context);

    terminal->context = NULL;
}

void terminal_destroy(terminal_t *terminal) {
    if (terminal == NULL) {
        return;
    }

    terminal_deinit(terminal);
    if (terminal->name != NULL) {
        free(terminal->name);
    }
    free(terminal);
}