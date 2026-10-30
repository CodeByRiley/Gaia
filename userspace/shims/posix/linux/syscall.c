#include <lib/services.h>
#include <shims/posix/linux/syscall.h>
int64_t linux_shim_syscall(int64_t n, int64_t a, int64_t b, int64_t c,
                           int64_t d, int64_t e, int64_t f) {
  return gaia_call(n, a, b, c, d, e, f);
}
