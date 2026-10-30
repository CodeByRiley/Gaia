#pragma once
#include <stdint.h>
struct linux_shim_timespec {
  int64_t seconds, nanoseconds;
};
_Static_assert(sizeof(struct linux_shim_timespec) == 16, "Linux timespec layout");
