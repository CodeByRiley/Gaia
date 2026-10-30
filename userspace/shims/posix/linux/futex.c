#include <shims/posix/linux/futex.h>
#include <shims/posix/linux/syscall.h>
int64_t linux_shim_futex_wait(uint32_t *p, uint32_t expected,
                              const struct linux_shim_timespec *timeout) {
  if (timeout)
    return -38;
  return linux_shim_syscall(202, (int64_t)(uintptr_t)p, 0, expected, 0, 0, 0);
}
int64_t linux_shim_futex_wake(uint32_t *p, uint32_t count) {
  return linux_shim_syscall(202, (int64_t)(uintptr_t)p, 1, count, 0, 0, 0);
}
