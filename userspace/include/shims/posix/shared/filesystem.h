#pragma once
#include <lib/services.h>
int64_t shim_open(const char *, unsigned);
int64_t shim_close(int);
int64_t shim_read(int, void *, size_t);
int64_t shim_write(int, const void *, size_t);
