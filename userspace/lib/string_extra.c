/* userspace/lib/string_extra.c , the rest of the str* family.
 *
 * Copies, concat, reverse-find, substring search, case-insensitive
 * compares, and a stubbed strerror. None of them are particularly fast;
 * they exist so DOOM and the shell link cleanly.
 */
#include <lib/syscall.h>
#include <include/string.h>

/* strcpy(3) , caller guarantees dst is big enough. */
char *strcpy(char *dst, const char *src) { char *d=dst; while ((*d++=*src++)); return dst; }

/* strncpy(3) , pads dst with NULs up to n. Doesn't guarantee NUL termination. */
char *strncpy(char *dst, const char *src, size_t n) { size_t i=0; for (;i<n&&src[i];i++)dst[i]=src[i]; for(;i<n;i++)dst[i]=0; return dst; }

/* strcat(3) , appends src after dst's NUL. */
char *strcat(char *dst, const char *src) { strcpy(dst+strlen(dst), src); return dst; }

/* strncat(3) , appends up to n bytes; always writes a NUL at dst[len+n]. */
char *strncat(char *dst, const char *src, size_t n) { size_t l=strlen(dst); for (size_t i=0;i<n&&src[i];i++)dst[l+i]=src[i]; dst[l+n]=0; return dst; }

/* strrchr(3) , pointer to last occurrence of c, NULL if absent. */
char *strrchr(const char *s, int c) { const char *r=0; while (*s){ if (*s==c) r=s; s++; } return c==0?(char*)s:(char*)r; }

/* strstr(3) , naive O(n*m) substring search. */
char *strstr(const char *h, const char *n) {
    if (!*n) return (char*)h;
    for (; *h; h++) {
        const char *a=h, *b=n;
        while (*a && *b && *a==*b) { a++; b++; }
        if (!*b) return (char*)h;
    }
    return 0;
}

/* strcasecmp(3) , ASCII-only case folding (A..Z → a..z). */
int strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a>='A'&&*a<='Z')?*a+32:*a;
        char cb = (*b>='A'&&*b<='Z')?*b+32:*b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* strncasecmp(3) , same as strcasecmp but bounded by n. */
int strncasecmp(const char *a, const char *b, size_t n) {
    while (n && *a && *b) {
        char ca = (*a>='A'&&*a<='Z')?*a+32:*a;
        char cb = (*b>='A'&&*b<='Z')?*b+32:*b;
        if (ca != cb) return ca - cb;
        a++; b++; n--;
    }
    return n ? ((int)(unsigned char)*a - (int)(unsigned char)*b) : 0;
}

/* strerror(3). The table covers every code the kernel currently reports;
 * unknown values deliberately retain their identity instead of pretending to
 * describe a different failure. */
char *strerror(int e) {
    switch (e) {
    case 0: return "Success";
    case 1: return "Operation not permitted";
    case 2: return "No such file or directory";
    case 3: return "No such process";
    case 4: return "Interrupted system call";
    case 5: return "Input/output error";
    case 6: return "No such device or address";
    case 9: return "Bad file descriptor";
    case 10: return "No child processes";
    case 11: return "Resource temporarily unavailable";
    case 12: return "Cannot allocate memory";
    case 13: return "Permission denied";
    case 14: return "Bad address";
    case 16: return "Device or resource busy";
    case 17: return "File exists";
    case 19: return "No such device";
    case 20: return "Not a directory";
    case 21: return "Is a directory";
    case 22: return "Invalid argument";
    case 23: return "Too many open files in system";
    case 24: return "Too many open files";
    case 25: return "Inappropriate ioctl for device";
    case 26: return "Text file busy";
    case 27: return "File too large";
    case 28: return "No space left on device";
    case 29: return "Illegal seek";
    case 30: return "Read-only file system";
    case 31: return "Too many links";
    case 32: return "Broken pipe";
    case 34: return "Numerical result out of range";
    case 35: return "Resource deadlock avoided";
    case 36: return "File name too long";
    case 37: return "No locks available";
    case 38: return "Function not implemented";
    case 39: return "Directory not empty";
    case 40: return "Too many levels of symbolic links";
    case 75: return "Value too large for defined data type";
    case 88: return "Socket operation on non-socket";
    case 89: return "Destination address required";
    case 90: return "Message too long";
    case 91: return "Protocol wrong type for socket";
    case 93: return "Protocol not supported";
    case 97: return "Address family not supported";
    case 98: return "Address already in use";
    case 99: return "Cannot assign requested address";
    case 100: return "Network is down";
    case 101: return "Network is unreachable";
    case 103: return "Software caused connection abort";
    case 104: return "Connection reset by peer";
    case 105: return "No buffer space available";
    case 106: return "Transport endpoint is already connected";
    case 107: return "Transport endpoint is not connected";
    case 108: return "Cannot send after transport endpoint shutdown";
    case 110: return "Connection timed out";
    case 111: return "Connection refused";
    case 113: return "No route to host";
    default: return "Unknown error";
    }
}
