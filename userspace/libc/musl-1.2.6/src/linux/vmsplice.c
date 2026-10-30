#define _GNU_SOURCE
#include "syscall.h"
#include <fcntl.h>

ssize_t vmsplice(int fd, const struct iovec *iov, size_t cnt, unsigned flags) {
  return syscall(SYS_vmsplice, fd, iov, cnt, flags);
}
