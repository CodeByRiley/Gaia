#include "syscall.h"
#include <unistd.h>

int fdatasync(int fd) { return syscall_cp(SYS_fdatasync, fd); }
