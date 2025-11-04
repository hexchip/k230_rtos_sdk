#include <stdio.h>

#include <display_manager.h>

#include <rtthread.h>

static const char * BUFFER_KEY = "buffer key";

int main(int argc, char *argv[]) {
  printf("hello world1.\n");

  display_manager_screen_info_t screen_info;
  int err = display_manager_get_screen_info(&screen_info);

  if (err) {
    printf("display_manager_get_screen_info error = %d\n", err);
  }

  err = display_manager_access_display();

  if (err) {
    printf("display_manager_access_display error = %d\n", err);
  }

  printf("hello world2.\n");

  size_t buffer_size = screen_info.width * screen_info.height * sizeof(uint8_t) * 3;
  uint8_t *px_map = display_manager_create_buffer(&BUFFER_KEY, buffer_size);

  for(size_t i = 0; i < buffer_size; i++) {
    px_map[i] = 233;
  }

  display_manager_area_t area = {
    .x1 = 0,
    .y1 = 0,
    .x2 = 640,
    .y2 = 480
  };

  err = display_manager_screen_flush(&area, px_map);

  if (err) {
    printf("display_manager_screen_flush error = %d\n", err);
  }

  display_manager_destroy_buffer(&BUFFER_KEY, px_map);

  rt_thread_mdelay(5000);

  err = display_manager_leave_display();

  if (err) {
    printf("display_manager_leave_display error = %d\n", err);
  }

  printf("hello world3.\n");
  return 0;
}
