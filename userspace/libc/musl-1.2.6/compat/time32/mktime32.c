#include "time32.h"
#include <errno.h>
#include <stdint.h>
#include <time.h>

time32_t __mktime32(struct tm *tm) {
  struct tm tmp = *tm;
  time_t t      = mktime(&tmp);
  if (t < INT32_MIN || t > INT32_MAX) {
    errno = EOVERFLOW;
    return -1;
  }
  *tm = tmp;
  return t;
}
