#include <shims/posix/shared/memory.h>
int64_t shim_map(void *p, size_t n, unsigned f) { return gaia_map(p, n, f); }
int64_t shim_unmap(void *p, size_t n) { return gaia_unmap(p, n); }
