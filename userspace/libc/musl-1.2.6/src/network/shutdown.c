#include "syscall.h"
#include <sys/socket.h>

int shutdown(int fd, int how) { return socketcall(shutdown, fd, how, 0, 0, 0, 0); }
