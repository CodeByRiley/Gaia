/* <gaia/system.h> , scheduler ticks, memory statistics, and power control.
 *
 * For wall-clock and monotonic time use clock_gettime() from musl's
 * <time.h>; it reaches the same kernel clock.
 */
#ifndef GAIA_SDK_SYSTEM_H
#define GAIA_SDK_SYSTEM_H

#include <gaia/abi.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Scheduler ticks since boot. The tick rate is whatever the timer was
 * calibrated to on this boot, so use ticks for ordering and short relative
 * waits, not for measuring wall time. */
long get_ticks(void);
/* Sleep for at least `n` scheduler ticks. */
long sleep_ticks(unsigned long n);
/* Give up the rest of this time slice. */
long yield(void);

/* Physical frame usage. Returns 0 on success. */
long mem_stats(struct mem_stats *out);

/* Flush every mounted volume's dirty data to its device. */
long fs_sync(void);

/* Power off or restart after `time` seconds. `reason` is logged and may be
 * NULL. */
long sys_shutdown(int time, const char *reason);
long sys_reboot(int time);

#ifdef __cplusplus
}
#endif

#endif
