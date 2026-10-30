#include "syscall.h"
#include <sys/ioctl.h>
#include <termios.h>

int tcdrain(int fd) { return syscall_cp(SYS_ioctl, fd, TCSBRK, 1); }
