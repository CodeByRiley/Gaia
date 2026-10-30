#include "syscall.h"
#include <unistd.h>

int fsync(int fd) { return syscall_cp(SYS_fsync, fd); }
