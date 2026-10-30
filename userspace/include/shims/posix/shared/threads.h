#pragma once
#include <lib/services.h>
/* Native Gaia threads; this does not emulate Linux clone/pthread semantics. */
int64_t shim_thread_create(uintptr_t entry, void *stack, void *argument);
int64_t shim_thread_join(int64_t thread);
