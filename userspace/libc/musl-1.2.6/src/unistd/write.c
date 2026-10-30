#include "syscall.h"
#include <unistd.h>

ssize_t write(int fd, const void *buf, size_t count) {
  return syscall_cp(SYS_write, fd, buf, count);
}
