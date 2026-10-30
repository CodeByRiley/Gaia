#include "syscall.h"
#include <sys/time.h>
#include <unistd.h>

unsigned alarm(unsigned seconds) {
  struct itimerval it = {.it_value.tv_sec = seconds}, old = {0};
  setitimer(ITIMER_REAL, &it, &old);
  return old.it_value.tv_sec + !!old.it_value.tv_usec;
}
