#include "aio_impl.h"
#include "syscall.h"
#include <errno.h>
#include <unistd.h>

static int dummy(int fd) { return fd; }

weak_alias(dummy, __aio_close);

int close(int fd) {
  fd    = __aio_close(fd);
  int r = __syscall_cp(SYS_close, fd);
  if (r == -EINTR)
    r = 0;
  return __syscall_ret(r);
}
