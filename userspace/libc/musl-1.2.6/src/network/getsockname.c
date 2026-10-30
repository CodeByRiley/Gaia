#include "syscall.h"
#include <sys/socket.h>

int getsockname(int fd, struct sockaddr *restrict addr, socklen_t *restrict len) {
  return socketcall(getsockname, fd, addr, len, 0, 0, 0);
}
