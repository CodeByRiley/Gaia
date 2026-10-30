#pragma once
#include "abi.h"
int64_t linux_shim_futex_wait(uint32_t *, uint32_t, const struct linux_shim_timespec *);
int64_t linux_shim_futex_wake(uint32_t *, uint32_t);
