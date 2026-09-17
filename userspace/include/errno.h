/* userspace/include/errno.h , POSIX-style error codes. */
#ifndef ERRNO_H
#define ERRNO_H

#include "utilities/types.h"

/* MinGW's host headers define errno as a DLL-backed function-like macro.
 * Gaia's freestanding legacy libc owns a simple process-global integer instead.
 * Undefine it before declaring the ABI symbol so PE and ELF agree. */
#ifdef errno
#undef errno
#endif

extern int errno;

/*
 * If compiling under C23 or newer, use the typed enum.
 * Otherwise, fall back to standard C99/C17 untyped enum for DOOM.
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
enum errornum : uchar {
#else
enum errornum {
#endif
    EPERM        = 1,
    ENOENT       = 2,
    ESRCH        = 3,
    EINTR        = 4,
    EIO          = 5,
    ENXIO        = 6,
    E2BIG        = 7,
    ENOEXEC      = 8,
    EBADF        = 9,
    ECHILD       = 10,
    EAGAIN       = 11,
    ENOMEM       = 12,
    EACCES       = 13,
    EFAULT       = 14,
    ENOTBLK      = 15,
    EBUSY        = 16,
    EEXIST       = 17,
    EXDEV        = 18,
    ENODEV       = 19,
    ENOTDIR      = 20,
    EISDIR       = 21,
    EINVAL       = 22,
    ENFILE       = 23,
    EMFILE       = 24,
    ENOTTY       = 25,
    ETXTBSY      = 26,
    EFBIG        = 27,
    ENOSPC       = 28,
    ESPIPE       = 29,
    EROFS        = 30,
    EMLINK       = 31,
    EPIPE        = 32,
    EDOM         = 33,
    ERANGE       = 34,
    EDEADLK      = 35,
    ENAMETOOLONG = 36,
    ENOLCK       = 37,
    ENOSYS       = 38,
    ENOTEMPTY    = 39,
    ELOOP        = 40,

    /* Extended file and data errors */
    EOVERFLOW    = 75,

    /*
     * Socket and network errors.
     * These retain their Linux values; they are intentionally non-contiguous.
     */
    ENOTSOCK     = 88,
    EDESTADDRREQ = 89,
    EMSGSIZE      = 90,
    EPROTOTYPE    = 91,

    EPROTONOSUPPORT = 93,
    EAFNOSUPPORT    = 97,
    EADDRINUSE     = 98,
    EADDRNOTAVAIL  = 99,
    ENETDOWN       = 100,
    ENETUNREACH    = 101,
    ECONNABORTED   = 103,
    ECONNRESET     = 104,
    ENOBUFS        = 105,
    EISCONN        = 106,
    ENOTCONN       = 107,
    ESHUTDOWN      = 108,
    ETIMEDOUT      = 110,
    ECONNREFUSED   = 111,
    EHOSTUNREACH   = 113,
};


#endif
