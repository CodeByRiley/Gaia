#define _GNU_SOURCE
#include "syscall.h"
#include <sched.h>

int unshare(int flags) { return syscall(SYS_unshare, flags); }
