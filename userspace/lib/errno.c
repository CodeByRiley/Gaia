/* userspace/lib/errno.c , legacy-libc errno storage and syscall conversion.
 *
 * Single translation unit owns the storage; headers expose it via extern.
 * No thread-local variant: this OS is single-threaded per process for now.
 */
#include <include/errno.h>
#include <lib/syscall.h>

int errno = 0;

/* Match musl's __syscall_ret contract for the hand-rolled libc. The kernel
 * reserves [-4095, -1] for failures; values outside that range are ordinary
 * successful results and must be passed through unchanged. */
long syscall_result(sysarg_t result) {
    if (result < 0 && result >= -4095) {
        errno = (int)-result;
        return -1;
    }
    return (long)result;
}
