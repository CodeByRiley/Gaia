#define _BSD_SOURCE
#include "syscall.h"
#include <errno.h>
#include <unistd.h>

int brk(void *end) { return __syscall_ret(-ENOMEM); }
