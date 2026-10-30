#include "syscall.h"
#include <sys/socket.h>

int bind(int fd, const struct sockaddr *addr, socklen_t len) {
  return socketcall(bind, fd, addr, len, 0, 0, 0);
}
