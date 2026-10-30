/* userspace/include/gaia/bits/syscall_abi.h , in-tree forwarder.
 *
 * The kernel owns the one definition of every syscall payload, in
 * kernel/arch/syscall_abi.h, which depends on nothing but <stddef.h> and
 * <stdint.h>. Inside the Gaia tree that file is reachable through -I ../kernel,
 * so this forwards to it.
 *
 * `make sdk` replaces this file in the staged SDK with a verbatim copy of the
 * kernel header, so an installed <gaia/abi.h> carries the same bytes the
 * kernel was compiled with and has no mirrored structs to drift.
 */
#include <arch/syscall_abi.h>
