#define _GNU_SOURCE
#include "syscall.h"
#include <unistd.h>

int syncfs(int fd) { return syscall(SYS_syncfs, fd); }
