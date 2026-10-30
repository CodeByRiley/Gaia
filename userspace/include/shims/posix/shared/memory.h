#pragma once
#include <lib/services.h>
int64_t shim_map(void *, size_t, unsigned);
int64_t shim_unmap(void *, size_t);
