#include <shims/posix/linux/errno.h>
int linux_shim_error(int64_t result) { return result < 0 && result >= -4095 ? (int)-result : 0; }
