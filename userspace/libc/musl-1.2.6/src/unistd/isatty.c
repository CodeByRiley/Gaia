#include "syscall.h"
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>

int isatty(int fd) {
  struct winsize wsz;
  /* +1 converts from error status (0/-1) to boolean (1/0) */
  return syscall(SYS_ioctl, fd, TIOCGWINSZ, &wsz) + 1;
}
