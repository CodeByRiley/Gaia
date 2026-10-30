#include <shims/posix/shared/time.h>
int64_t shim_sleep(uint64_t n) { return gaia_sleep(n); }
int64_t shim_monotonic_time(uint64_t *n) { return gaia_time(n); }
