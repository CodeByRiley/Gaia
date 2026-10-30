#include <shims/posix/shared/signals.h>
int64_t shim_process_terminate(int64_t process) {
  return gaia_terminate(process);
}
