#define _GNU_SOURCE
#include "syscall.h"
#include <fcntl.h>

int fallocate(int fd, int mode, off_t base, off_t len) {
  return syscall(SYS_fallocate, fd, mode, __SYSCALL_LL_E(base), __SYSCALL_LL_E(len));
}
