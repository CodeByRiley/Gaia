#include "time32.h"
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

struct stat32;

int __stat_time32(const char *restrict path, struct stat32 *restrict st32) {
  struct stat st;
  int r = stat(path, &st);
  if (!r)
    memcpy(st32, &st, offsetof(struct stat, st_atim));
  return r;
}
