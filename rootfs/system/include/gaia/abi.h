/* <gaia/abi.h> , data structures shared with the Gaia kernel.
 *
 * Every struct here crosses the syscall boundary, so its layout is the ABI.
 * The definitions are the kernel's own (see gaia/bits/syscall_abi.h), which
 * carries _Static_assert checks on each size and the offsets that matter.
 *
 * GAIA_SDK_ABI_VERSION is bumped whenever a public declaration changes in a
 * way an already-compiled program would notice: a payload layout, a
 * function signature, or the meaning of a return value. Adding a function
 * does not bump it. libgaia is a static library, so a program keeps the ABI
 * it was linked with; the version exists for source-level #if checks.
 */
#ifndef GAIA_SDK_ABI_H
#define GAIA_SDK_ABI_H

#define GAIA_SDK_ABI_VERSION 1

#include <gaia/bits/syscall_abi.h>

#endif
