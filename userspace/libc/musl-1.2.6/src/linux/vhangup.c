#define _GNU_SOURCE
#include "syscall.h"
#include <unistd.h>

int vhangup(void) { return syscall(SYS_vhangup); }
