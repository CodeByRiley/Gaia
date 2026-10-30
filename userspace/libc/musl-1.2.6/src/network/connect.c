#include "syscall.h"
#include <sys/socket.h>

int connect(int fd, const struct sockaddr *addr, socklen_t len) {
  return socketcall_cp(connect, fd, addr, len, 0, 0, 0);
}
