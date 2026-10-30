#define _BSD_SOURCE
#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>

int cfsetspeed(struct termios *tio, speed_t speed) {
  int r = cfsetospeed(tio, speed);
  if (!r)
    cfsetispeed(tio, 0);
  return r;
}
