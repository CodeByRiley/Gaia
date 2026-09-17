/* kernel/utilities/errno.h , error numbers returned across the syscall ABI.
 *
 * A syscall that fails returns the NEGATED code, which is the Linux
 * convention and the reason these numbers are the Linux ones rather than
 * anything of our own: musl's __syscall_ret turns a return in [-4095, -1]
 * into errno = -ret and a -1 result without knowing anything about Gaia, so a
 * musl-linked caller gets working errno and strerror() for free.
 *
 * The same numbers appear in userspace/include/errno.h for the hand-rolled
 * libc, and in musl's own headers. Nothing includes another's copy, the
 * kernel cannot reach userspace headers and musl must not see ours, so what
 * keeps the three agreeing is that all of them are the POSIX/Linux values.
 * Add a code here only with the number Linux uses for it.
 *
 * Only the codes the kernel actually returns are listed. A syscall that has
 * not been converted still returns a bare -1, which reads as -EPERM; that is
 * wrong but harmless, and is why new code should return a real code.
 */
#ifndef KERNEL_ERRNO_H
#define KERNEL_ERRNO_H

/* Permission and process errors */
#define EPERM            1  /* Operation not permitted */
#define ESRCH            3  /* No such process */
#define EINTR            4  /* Interrupted system call */
#define ECHILD          10  /* No child processes */
#define EACCES          13  /* Permission denied */

/* File descriptor errors */
#define EBADF            9  /* Bad file descriptor */
#define EMFILE           24  /* Too many open files for this process */
#define ENFILE           23  /* Too many open files in the system */

/* File and directory lookup errors */
#define ENOENT            2  /* No such file or directory */
#define EEXIST           17  /* File already exists */
#define ENOTDIR          20  /* Not a directory */
#define EISDIR           21  /* Is a directory */
#define ENAMETOOLONG     36  /* Filename too long */
#define ENOTEMPTY        39  /* Directory not empty */
#define ELOOP            40  /* Too many symbolic links */

/* File position, size, and link errors */
#define ETXTBSY          26  /* Text file busy */
#define EFBIG            27  /* File too large */
#define ESPIPE           29  /* Illegal seek */
#define EMLINK           31  /* Too many links */

/* File locking errors */
#define EDEADLK          35  /* Resource deadlock avoided */
#define ENOLCK           37  /* No locks available */

/* Filesystem and device errors */
#define EIO               5  /* Input/output error */
#define ENXIO             6  /* No such device or address */
#define ENODEV           19  /* No such device */
#define ENOSPC           28  /* No space left on device */
#define EROFS            30  /* Read-only filesystem */

/* Memory and address errors */
#define ENOMEM           12  /* Out of memory */
#define EFAULT           14  /* Bad address supplied by userspace */

/* Argument and range errors */
#define EINVAL           22  /* Invalid argument */
#define ERANGE           34  /* Result too large for the caller's buffer */
#define EOVERFLOW        75  /* Value too large for the defined data type */

/* Resource and synchronization errors */
#define EAGAIN           11  /* Try again */
#define EBUSY            16  /* Device or resource busy */

/* Pipe and stream errors */
#define EPIPE            32  /* Broken pipe */

/* Interface and implementation errors */
#define ENOTTY           25  /* Inappropriate ioctl for this device */
#define ENOSYS           38  /* Function not implemented */

/* Socket and network errors */
#define ENOTSOCK         88  /* Descriptor is not a socket */
#define EDESTADDRREQ     89  /* Destination address required */
#define EMSGSIZE         90  /* Message too large */
#define EPROTOTYPE       91  /* Protocol wrong type for socket */
#define EPROTONOSUPPORT  93  /* Protocol or socket type not supported */
#define EAFNOSUPPORT     97  /* Address family not supported */
#define EADDRINUSE       98  /* Address or port already in use */
#define EADDRNOTAVAIL    99  /* Cannot assign requested address */
#define ENETDOWN        100  /* Network is down */
#define ENETUNREACH     101  /* Network is unreachable */
#define ECONNABORTED    103  /* Connection aborted */
#define ECONNRESET      104  /* Connection reset by peer */
#define ENOBUFS         105  /* No buffer space available */
#define EISCONN         106  /* Transport endpoint is already connected */
#define ENOTCONN        107  /* Transport endpoint is not connected */
#define ESHUTDOWN       108  /* Cannot send after transport endpoint shutdown */
#define ETIMEDOUT       110  /* Connection timed out */
#define ECONNREFUSED    111  /* Connection refused */
#define EHOSTUNREACH    113  /* No route to host */

/* Kernel callers store errors as negative errno values, while diagnostic
 * APIs normally receive the positive number.  Keeping this here prevents
 * every driver and filesystem from growing a slightly different error-text
 * table. */
static inline const char *kernel_errno_string(int error) {
    switch (error < 0 ? -error : error) {
    case EPERM: return "Operation not permitted";
    case ENOENT: return "No such file or directory";
    case EIO: return "Input/output error";
    case ENXIO: return "No such device or address";
    case EBADF: return "Bad file descriptor";
    case EAGAIN: return "Resource temporarily unavailable";
    case ENOMEM: return "Out of memory";
    case EACCES: return "Permission denied";
    case EBUSY: return "Device or resource busy";
    case EEXIST: return "File exists";
    case ENODEV: return "No such device";
    case ENOTDIR: return "Not a directory";
    case EISDIR: return "Is a directory";
    case EINVAL: return "Invalid argument";
    case ENFILE: return "File table overflow";
    case EMFILE: return "Too many open files";
    case ENOSPC: return "No space left on device";
    case EROFS: return "Read-only filesystem";
    case ESPIPE: return "Illegal seek";
    case ENAMETOOLONG: return "File name too long";
    case ENOTEMPTY: return "Directory not empty";
    case ENOSYS: return "Function not implemented";
    case EOVERFLOW: return "Value too large";
    default: return "Unknown error";
    }
}

#endif /* KERNEL_ERRNO_H */
