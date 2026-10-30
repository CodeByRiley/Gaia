#include "fcntl.h"
#include "syscall.h"
#include <sys/time.h>

int utimes(const char *path, const struct timeval times[2]) {
  return __futimesat(AT_FDCWD, path, times);
}
