#define _GNU_SOURCE 1
#include "syscall.h"
#include <sys/mman.h>

int memfd_create(const char *name, unsigned flags) {
  return syscall(SYS_memfd_create, name, flags);
}
