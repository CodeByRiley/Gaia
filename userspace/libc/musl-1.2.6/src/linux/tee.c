#define _GNU_SOURCE
#include "syscall.h"
#include <fcntl.h>

ssize_t tee(int src, int dest, size_t len, unsigned flags) {
  return syscall(SYS_tee, src, dest, len, flags);
}
