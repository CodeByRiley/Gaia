#include "syscall.h"
#include <sys/ioctl.h>
#include <termios.h>

int tcgetwinsize(int fd, struct winsize *wsz) { return syscall(SYS_ioctl, fd, TIOCGWINSZ, wsz); }
