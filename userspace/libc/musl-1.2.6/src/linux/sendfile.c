#include "syscall.h"
#include <sys/sendfile.h>

ssize_t sendfile(int out_fd, int in_fd, off_t *ofs, size_t count) {
  return syscall(SYS_sendfile, out_fd, in_fd, ofs, count);
}
