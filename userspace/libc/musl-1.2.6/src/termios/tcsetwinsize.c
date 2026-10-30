#include "syscall.h"
#include <sys/ioctl.h>
#include <termios.h>

int tcsetwinsize(int fd, const struct winsize *wsz) {
  return syscall(SYS_ioctl, fd, TIOCSWINSZ, wsz);
}
