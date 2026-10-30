#include <shims/posix/shared/process.h>
int64_t shim_process_id(void) { return gaia_pid(); }
