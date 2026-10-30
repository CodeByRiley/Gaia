#include "syscall.h"
#include <sys/random.h>

ssize_t getrandom(void *buf, size_t buflen, unsigned flags) {
  return syscall_cp(SYS_getrandom, buf, buflen, flags);
}
