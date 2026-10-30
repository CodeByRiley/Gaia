#define _BSD_SOURCE
#include "syscall.h"
#include <dirent.h>
#include <limits.h>

int getdents(int fd, struct dirent *buf, size_t len) {
  if (len > INT_MAX)
    len = INT_MAX;
  return syscall(SYS_getdents, fd, buf, len);
}
