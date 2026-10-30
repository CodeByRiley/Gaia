#include <shims/posix/shared/threads.h>
int64_t shim_thread_create(uintptr_t entry, void *stack, void *argument) {
  return gaia_thread_create(entry, stack, argument);
}
int64_t shim_thread_join(int64_t thread) { return gaia_thread_join(thread); }
