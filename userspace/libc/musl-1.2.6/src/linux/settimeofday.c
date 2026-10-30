#define _BSD_SOURCE
#include "syscall.h"
#include <errno.h>
#include <sys/time.h>
#include <time.h>

int settimeofday(const struct timeval *tv, const struct timezone *tz) {
  if (!tv)
    return 0;
  if (tv->tv_usec >= 1000000ULL)
    return __syscall_ret(-EINVAL);
  return clock_settime(CLOCK_REALTIME,
                       &((struct timespec){.tv_sec = tv->tv_sec, .tv_nsec = tv->tv_usec * 1000}));
}
