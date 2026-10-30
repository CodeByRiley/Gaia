#include <lib/services.h>
#include <lib/syscall.h>
#define PTR(p) ((int64_t)(uintptr_t)(p))
int64_t gaia_call(int64_t n, int64_t a, int64_t b, int64_t c, int64_t d,
                  int64_t e, int64_t f) {
  register int64_t r10 __asm__("r10") = d, r8 __asm__("r8") = e,
                       r9 __asm__("r9") = f;
  int64_t result;
  __asm__ volatile("syscall"
                   : "=a"(result)
                   : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                   : "rcx", "r11", "memory");
  return result;
}
int64_t gaia_open(const char *path, unsigned flags) {
  int native = (flags & GAIA_WRITE) ? ((flags & GAIA_READ) ? 2 : 1) : 0;
  if (flags & GAIA_CREATE)
    native |= 64;
  if (flags & GAIA_TRUNCATE)
    native |= 512;
  if (flags & GAIA_EXCLUSIVE)
    native |= 128;
  return gaia_call(SYS_OPEN, PTR(path), native, 0666, 0, 0, 0);
}
int64_t gaia_close(int fd) { return gaia_call(SYS_CLOSE, fd, 0, 0, 0, 0, 0); }
int64_t gaia_read(int fd, void *p, size_t n) {
  return gaia_call(SYS_READ, fd, PTR(p), n, 0, 0, 0);
}
int64_t gaia_write(int fd, const void *p, size_t n) {
  return gaia_call(SYS_WRITE, fd, PTR(p), n, 0, 0, 0);
}
int64_t gaia_seek(int fd, int64_t n, int whence) {
  return gaia_call(SYS_LSEEK, fd, n, whence, 0, 0, 0);
}
int64_t gaia_map(void *at, size_t n, unsigned prot) {
  return gaia_call(SYS_MMAP, PTR(at), n, prot, 0x22 | (at ? 0x10 : 0), -1, 0);
}
int64_t gaia_unmap(void *p, size_t n) {
  return gaia_call(SYS_MUNMAP, PTR(p), n, 0, 0, 0, 0);
}
int64_t gaia_protect(void *p, size_t n, unsigned prot) {
  return gaia_call(SYS_MPROTECT, PTR(p), n, prot, 0, 0, 0);
}
int64_t gaia_pid(void) { return gaia_call(SYS_GET_PID, 0, 0, 0, 0, 0, 0); }
int64_t gaia_tid(void) { return gaia_call(186, 0, 0, 0, 0, 0, 0); }
int64_t gaia_yield(void) { return gaia_call(SYS_YIELD, 0, 0, 0, 0, 0, 0); }
int64_t gaia_sleep(uint64_t ns) {
  int64_t ts[2] = {(int64_t)(ns / 1000000000), ns % 1000000000};
  return gaia_call(SYS_NANOSLEEP, PTR(ts), 0, 0, 0, 0, 0);
}
int64_t gaia_time(uint64_t *ns) {
  int64_t ts[2];
  int64_t r = gaia_call(SYS_CLOCK_GETTIME, 1, PTR(ts), 0, 0, 0, 0);
  if (!r)
    *ns = (uint64_t)ts[0] * 1000000000 + ts[1];
  return r;
}

int64_t gaia_thread_create(uintptr_t e, void *s, void *a) {
  return gaia_call(SYS_THREAD_CREATE, e, PTR(s), PTR(a), 0, 0, 0);
}
int64_t gaia_thread_join(int64_t t) {
  return gaia_call(SYS_THREAD_JOIN, t, 0, 0, 0, 0, 0);
}
int64_t gaia_terminate(int64_t p) {
  return gaia_call(SYS_KILL, p, 15, 0, 0, 0, 0);
}
