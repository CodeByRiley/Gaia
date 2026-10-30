#include "syscall.h"
#include <sys/swap.h>

int swapon(const char *path, int flags) { return syscall(SYS_swapon, path, flags); }

int swapoff(const char *path) { return syscall(SYS_swapoff, path); }
