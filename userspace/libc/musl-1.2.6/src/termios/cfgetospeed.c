#define _BSD_SOURCE
#include <sys/ioctl.h>
#include <termios.h>

speed_t cfgetospeed(const struct termios *tio) { return tio->c_cflag & CBAUD; }

speed_t cfgetispeed(const struct termios *tio) {
  return (tio->c_cflag & CIBAUD) / (CIBAUD / CBAUD);
}
