#ifndef GAIA_SERVICES_H
#define GAIA_SERVICES_H
#include <stddef.h>
#include <stdint.h>
/* Platform-neutral service contract: negative Gaia errors, owned descriptors,
 * page-rounded anonymous mappings, and monotonic nanoseconds. No TLS/errno. */
enum gaia_file_flags {
  GAIA_READ = 1,
  GAIA_WRITE = 2,
  GAIA_CREATE = 4,
  GAIA_TRUNCATE = 8,
  GAIA_EXCLUSIVE = 16
};
int64_t gaia_open(const char *, unsigned);
int64_t gaia_close(int);
int64_t gaia_read(int, void *, size_t);
int64_t gaia_write(int, const void *, size_t);
int64_t gaia_seek(int, int64_t, int);
int64_t gaia_map(void *, size_t, unsigned);
int64_t gaia_unmap(void *, size_t);
int64_t gaia_protect(void *, size_t, unsigned);
int64_t gaia_pid(void);
int64_t gaia_tid(void);
int64_t gaia_sleep(uint64_t);
int64_t gaia_time(uint64_t *);
int64_t gaia_yield(void);
int64_t gaia_thread_create(uintptr_t, void *, void *);
int64_t gaia_thread_join(int64_t);
int64_t gaia_terminate(int64_t);
int64_t gaia_call(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t,
                  int64_t);
#endif
