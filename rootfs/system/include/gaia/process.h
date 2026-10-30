/* <gaia/process.h> , executable lookup, launch, inspection, and bounded
 * lifecycle waits.
 *
 * This is the one declaration of the process API: userspace/lib/process.h
 * includes it, so libgaia and its SDK consumers compile against the same
 * prototypes.
 *
 * These are Gaia-native launch primitives, not POSIX. spawn() starts a new
 * program image; there is no fork(). A POSIX layer can be built over them,
 * but nothing here pretends to be one.
 */
#ifndef GAIA_SDK_PROCESS_H
#define GAIA_SDK_PROCESS_H

#include <gaia/abi.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROCESS_PATH_MAX 260
#define PROCESS_DEFAULT_PATH "/bin:/usr/bin:/usr/local/bin:/system/bin"
#define PROCESS_WAIT_FOREVER UINT32_MAX

enum process_wait_result {
  PROCESS_ERROR   = -1,
  PROCESS_TIMEOUT = 0,
  PROCESS_EXITED  = 1,
};

/* Raw launch syscalls. `path` is used as given (relative to the caller's
 * working directory); argv is NULL-terminated and argv[0] is the program
 * name the child sees.
 *
 * exec() blocks until the child exits and returns its exit code, or a
 * negative value if it could not be started , which a child exiting with a
 * negative code is indistinguishable from. spawn() returns the child's pid
 * immediately, or a negative value. */
long exec(const char *path, char *const argv[]);
long spawn(const char *path, char *const argv[]);

/* Resolve `program` directly when it contains a slash, otherwise search a
 * colon-separated path. NULL/empty search_path uses PROCESS_DEFAULT_PATH.
 * An extensionless name probes the exact spelling, then .elf and .exe.
 * Returns 0 and fills `out`, or -1. */
int process_resolve(const char *program, const char *search_path, char *out, size_t capacity);

/* spawn()/exec() after process_resolve(). -1 when nothing resolved. */
long process_spawn(const char *program, char *const argv[], const char *search_path);
long process_exec(const char *program, char *const argv[], const char *search_path);

/* Copy up to `capacity` rows of the kernel process table into `out` and
 * return how many were written, or -1. */
long process_snapshot(struct proc_info *out, size_t capacity);
/* 1 and fills `out` (which may be NULL) when `pid` is listed, 0 when it is
 * not, -1 on error. */
int process_get(int pid, struct proc_info *out);
/* 1 while `pid` exists and is neither ZOMBIE nor DEAD, 0 otherwise, -1 on
 * error. */
int process_is_alive(int pid);

/* Wait until a process disappears or reaches ZOMBIE/DEAD. Gaia does not yet
 * expose child exit status through proc_list, so this reports lifecycle only.
 * timeout_ticks == 0 performs one poll. Returns an enum process_wait_result. */
int process_wait(int pid, uint32_t timeout_ticks, struct proc_info *last_seen);

/* The caller's own pid. */
long get_pid(void);

#ifdef __cplusplus
}
#endif

#endif
