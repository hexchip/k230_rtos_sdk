#include <stdio.h>

#include <display_manager.h>

#include <rtthread.h>

int main(int argc, char *argv[]) {
  printf("hello world1.\n");

  display_manager_access_display();

  printf("hello world2.\n");

  rt_thread_mdelay(5000);

  display_manager_leave_display();

  printf("hello world3.\n");
  return 0;
}
