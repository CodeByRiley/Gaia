#define _GNU_SOURCE
#include "syscall.h"
#include <sched.h>

int setns(int fd, int nstype) { return syscall(SYS_setns, fd, nstype); }
