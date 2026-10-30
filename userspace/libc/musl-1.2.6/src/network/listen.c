#include "syscall.h"
#include <sys/socket.h>

int listen(int fd, int backlog) { return socketcall(listen, fd, backlog, 0, 0, 0, 0); }
