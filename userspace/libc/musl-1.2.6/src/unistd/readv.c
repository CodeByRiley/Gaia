#include "syscall.h"
#include <sys/uio.h>

ssize_t readv(int fd, const struct iovec *iov, int count) {
  return syscall_cp(SYS_readv, fd, iov, count);
}
