#include <shims/posix/shared/filesystem.h>
int64_t shim_open(const char *p, unsigned f) { return gaia_open(p, f); }
int64_t shim_close(int f) { return gaia_close(f); }
int64_t shim_read(int f, void *p, size_t n) { return gaia_read(f, p, n); }
int64_t shim_write(int f, const void *p, size_t n) { return gaia_write(f, p, n); }
