#include "time32.h"
#include <signal.h>
#include <time.h>

int __sigtimedwait_time32(const sigset_t *restrict set, siginfo_t *restrict si,
                          const struct timespec32 *restrict ts32) {
  return sigtimedwait(
      set, si, !ts32 ? 0 : (&(struct timespec){.tv_sec = ts32->tv_sec, .tv_nsec = ts32->tv_nsec}));
}
