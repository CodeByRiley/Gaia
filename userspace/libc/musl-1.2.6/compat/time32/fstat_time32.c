#include "time32.h"
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

struct stat32;

int __fstat_time32(int fd, struct stat32 *restrict st32) {
  struct stat st;
  int r = fstat(fd, &st);
  if (!r)
    memcpy(st32, &st, offsetof(struct stat, st_atim));
  return r;
}
