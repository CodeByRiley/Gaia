#pragma once
#include <stdint.h>
/* Raw Linux x86-64 convention: negative errno, no userspace errno mutation. */
int64_t linux_shim_syscall(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t,
                           int64_t);
