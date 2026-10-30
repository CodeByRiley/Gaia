# Gaia Linux x86-64 syscall compatibility

Status of the Linux syscall personality that musl programs (and the native
GCC 17 / Binutils toolchain) run on. Dispatcher: `kernel/arch/syscall.c`;
numbers: `kernel/arch/syscalls.def`. Audited 2026-10-31 by reading each
handler, not by listing switch cases.

Status key: **Impl** — semantics implemented and tested · **Partial** — works,
with a documented deviation · **Stub** — fixed answer, by design · **ENOSYS** —
recognised, deliberately refused · **Missing** — no handler (logged once per
process as `syscall: pid=N name: unimplemented X (n)`).

## Matrix

### Files, paths, descriptors

| # | Call | Status | Notes |
|---|---|---|---|
| 0, 1 | read, write | Impl | console, pipe, socket, file; `read(fd, buf, 0)` on the console is now 0, not EFAULT |
| 2, 257 | open, openat | Impl | dirfd honoured; `O_CLOEXEC`, `O_NONBLOCK`, `O_DIRECTORY`, `O_EXCL`; directories refuse write access (EISDIR). A created file gets `mode & ~umask` where the filesystem stores modes |
| 3 | close | Impl | unused fd 0–2 still means "console" and closes to success |
| 4, 6 | stat, lstat | Impl | no symlinks exist, so lstat ≡ stat |
| 5 | fstat | Impl | |
| 262 | newfstatat | Impl | dirfd, `AT_EMPTY_PATH` (dir, file or cwd), `AT_SYMLINK_NOFOLLOW` accepted |
| 8 | lseek | Impl | seeking past EOF now holds on FAT too (was silently clamped) |
| 17, 18 | pread64, pwrite64 | Partial | position restored; seek/IO/restore is three VFS calls, so a *shared* open file can observe the moved offset |
| 19, 20 | readv, writev | Impl | |
| 21, 269, 439 | access, faccessat, faccessat2 | Impl | see access policy below; 269 takes no flags argument, 439 does — kept separate |
| 22, 293 | pipe, pipe2 | Impl | `O_CLOEXEC`, `O_NONBLOCK`; unknown flags EINVAL; EOF when last writer closes |
| 32, 33, 292 | dup, dup2, dup3 | Impl | dup clears FD_CLOEXEC; dup3 sets it on request, EINVAL on same fd/bad flags |
| 72 | fcntl | Partial | F_GETFD/F_SETFD real; F_DUPFD(_CLOEXEC); F_SETFL honours only O_NONBLOCK (per-slot, Linux keeps it per open-file); F_GETFL reports no access mode |
| 79, 80, 81 | getcwd, chdir, fchdir | Impl | |
| 83, 258 | mkdir, mkdirat | Impl | `mode & ~umask`, where the filesystem stores modes |
| 90, 91, 268 | chmod, fchmod, fchmodat | Impl | permission bits only, file type kept; EPERM on FAT (stores no modes) and for fds with no inode (pipe, console, socket — Linux would change the pipe inode). fchmodat2 (452) not provided |
| 95 | umask | Impl | per process, inherited with the cwd and kept across exec; default 022 |
| 84, 87, 263 | rmdir, unlink, unlinkat | Impl | `AT_REMOVEDIR`; EEXIST / ENOTEMPTY now reported by the VFS instead of EIO |
| 89, 267 | readlink, readlinkat | Impl | no symlinks: existing path → EINVAL (what realpath expects), missing → ENOENT |
| 217 | getdents64 | Impl | errors now propagate as their errno (was a bare -1 = EPERM) |
| 16 | ioctl | Partial | TIOCGWINSZ (fixed 80×25), TCGETS (zeroed), TCSETS* accepted; others ENOTTY |
| 7 | poll | Impl | console never reports readable (no wait queue) |
| 82, 86, 88, 92 | rename, link, symlink, chown | Missing | not yet needed by the toolchain run |

### Memory

| # | Call | Status | Notes |
|---|---|---|---|
| 9 | mmap | Partial | anonymous: lazy, zero-filled. **MAP_PRIVATE file mappings now supported** (copy at map time — a legal MAP_PRIVATE). MAP_SHARED file → ENODEV. MAP_FIXED fails on overlap instead of replacing (FIXED_NOREPLACE semantics). Arena is 256 MiB |
| 10 | mprotect | Partial | commits pages then changes PTEs; does not update the reservation record |
| 11 | munmap | Impl | |
| 12 | brk | Impl | |
| 25 | mremap | ENOSYS | musl realloc falls back to malloc+copy+free |
| 28 | madvise | Impl | NORMAL/RANDOM/SEQUENTIAL/WILLNEED are no-op hints; DONTNEED/FREE really drop pages (next touch reads zero); only for anonymous reservations, else EINVAL |

### Processes

| # | Call | Status | Notes |
|---|---|---|---|
| 39, 110 | getpid, getppid | Impl | |
| 56 | clone | Partial | **only `CLONE_VM \| CLONE_VFORK` (+ SIGCHLD)** — the shape musl's posix_spawn uses. Thread and fork-style clones → ENOSYS (pthread_create reports EAGAIN) |
| 57 | fork | ENOSYS | needs a copied address space; Gaia has no copy-on-write |
| 58 | vfork | Impl | real vfork: new pid, copied fd table/cwd/signals, parent parked until exec or exit |
| 59 | execve | Impl | argv + **envp** (up to 4096 entries / 512 KiB of strings), CLOEXEC honoured, caught signals reset, FS/GS/FPU reset, vfork parent released |
| 60, 231 | exit, exit_group | Partial | same handler; no multi-thread group semantics |
| 61 | wait4 | Partial | WNOHANG only; rusage zeroed |
| 62, 200, 234 | kill, tkill, tgkill | Impl | (existing signal subsystem) |
| 158 | arch_prctl | Impl | SET/GET FS and GS |
| 97, 302 | getrlimit, prlimit64 | Impl | real fixed limits for STACK (2 MiB), NOFILE (32), NPROC (32); others RLIM_INFINITY; sets succeed only if unchanged, else EPERM |
| 102, 104, 107, 108 | getuid, getgid, geteuid, getegid | Stub | always 0 — Gaia's single credential, matching the access policy |
| 98 | getrusage | Missing | as/ld call it for statistics and tolerate ENOSYS |
| 273 | set_robust_list | Missing | only needed with real pthreads |

### Runtime, time, signals

| # | Call | Status | Notes |
|---|---|---|---|
| 13, 14, 15, 127 | rt_sigaction, rt_sigprocmask, rt_sigreturn, rt_sigpending | Impl | existing |
| 24 | sched_yield | Impl | |
| 35 | nanosleep | Partial | `rem` always zero (sleep is not interruptible) |
| 63 | uname | Impl | reports `Gaia`, not Linux |
| 96, 228 | gettimeofday, clock_gettime | Partial | REALTIME and MONOTONIC only |
| 186, 218 | gettid, set_tid_address | Partial | clear-tid address is not stored |
| 202 | futex | Partial | WAIT/WAKE; timed WAIT → ENOSYS |
| 230, 318 | clock_nanosleep, getrandom | Missing | not observed in the toolchain run |

## Access policy

Gaia has no users: every process is uid/gid 0, and `access()` answers the
way Linux answers root. Existence is checked for real; R_OK and W_OK are then
granted (root bypasses r/w bits; nothing is mounted read-only); X_OK requires
at least one execute bit, the one check Linux still applies to root. ext2
stores real modes (files created with 0644 are *not* executable). FAT has no
permission bits; it now reports 0777 (0555 when read-only), as Linux vfat
does by default, so programs on FAT are executable. `execve` applies the same
X check (EACCES).

## ABI notes and deviations

- Raw syscalls return `-errno` in [-4095, -1]; musl converts. Every new
  handler copies user strings into the kernel first (`resolve_user_path_at`),
  so a bad path pointer is EFAULT, not a kernel fault. Existing exec/spawn/
  stat/mkdir/chdir/readdir_path handlers were moved onto the same path.
- One shared resolver serves every `*at` call: absolute paths ignore dirfd,
  relative ones resolve against the cwd (`AT_FDCWD`) or the directory fd's
  recorded path; non-directory dirfd → ENOTDIR, bad dirfd → EBADF.
- `FD_CLOEXEC` is on the descriptor slot (`TASK_FD_CLOEXEC`). Setting it on an
  unused fd 0–2 materialises an explicit console slot; after exec an emptied
  fd 0–2 still means "console", so CLOEXEC on stdio is effectively ignored.
- vfork children share the parent's reservation table; the parent cannot
  run until the child execs or exits, so the table is never used by both.
- `exit_group` from one thread does not stop sibling Gaia threads.
- Gaia's native `SYS_EXEC`/`SYS_SPAWN` still pass an **empty environment**
  and at most 16 arguments of 127 bytes. That is the Gaia API, not the Linux
  one; the shell no longer uses it.

## Diagnostics

- Unknown or refused calls log once per (pid, number):
  `syscall: pid=42 ld: unimplemented getrusage (98)` — the name comes from
  `kernel/arch/linux_syscall_names.h`, generated from musl's x86-64 table.
  Non-Linux numbers print as `non-Linux syscall N`. No argument is
  dereferenced for logging.
- `KERNEL_EXTRA_CFLAGS=-DSYSCALL_TRACE mingw32-make build-x86_64` logs the
  first use of every syscall by each process (`syscall-trace: ...`), which
  is the set a new port needs.

## Tests

- `userspace/libc/tests/syscall_compat.c` → `/usr/bin/muslsyscall.elf`:
  128 checks (paths, chmod/umask, `*at`, access, readlink, fds/CLOEXEC, pread/pwrite,
  seek-past-EOF, pipes, ids, rlimits, mmap/madvise/file mapping, 48 MiB
  malloc, posix_spawn argv/env/stdout-pipe/cwd/CLOEXEC/failure, vfork+execve,
  execve errors). Also builds on a Linux host as the reference; the one
  Linux/Gaia difference (MAP_SHARED file mappings) is checked as "maps or
  says ENODEV". `python tests/syscall_compat_test.py --iso build/test.iso`
  runs it on the FAT ramdisk; an earlier revision (112 checks) also passed
  with the whole suite itself started through `execve`.
- `tests/uvm_test.c`: new reservation-merge test.
- Run the suite against an ext2 root with
  `--disk build/disk-test-ext2.img` (q35/AHCI, as `run.bat` boots):
  **128/128**; on the FAT ramdisk 121/121 (the mode-storing checks see EPERM
  and are skipped). Linux host reference: 128/128 as root.
- `tests/gcc_native_smoke_test.py`: the toolchain ladder below.
- Small test ISO without the toolchain (~16 s, 82 MB):
  `IMG=build/disk-test.img GAIA_TOOLCHAIN=0 bash tools/create_disk.sh` then
  `DISK_IMAGE=build/disk-test.img OUT_ISO=build/test.iso bash tools/build_iso.sh`.

## Native toolchain status (FAT root, QEMU TCG, 1 GiB)

| Step | Result |
|---|---|
| `gcc --version`, `gcc -v` | pass |
| `gcc -print-search-dirs`, `-print-prog-name=cc1` | pass from any directory, now that the shell passes the resolved path as argv[0] |
| `gcc -S hello.c` | **pass**, typed bare in the shell (driver → posix_spawn → cc1) |
| `gcc -c hello.c` | **pass**, typed bare in the shell (cc1 → as, valid ELF object verified with readelf in the guest) |
| `gcc hello.c -o hello.elf` | **pass**: cc1 → as → collect2 → ld; static executable, runs and exits with its return code |
| `gcc -O2 -Wall -include stdio.h p.c -o p.elf` | **pass**: musl headers and libc; the program prints `native gcc on Gaia` |

Kernel bugs found by the toolchain and fixed: the 64-entry reservation table
filled within cc1's first allocations (adjacent reservations now merge);
FAT clamped `lseek` past EOF while reporting success, so every object file
BFD wrote was truncated.

## Remaining blockers

1. ~~Sysroot not packaged~~ , fixed: `tools/stage_native_sysroot.sh` (WSL)
   copies crt1/crti/crtn, libc, libm, libgaia and gaia.ld into
   `rootfs/system/lib`, libgcc/libgcov into `rootfs/system/lib/gcc/...`, and
   musl's headers into `rootfs/system/include`; both disk packagers now ship
   `system/lib` and `system/include`. Rerun it after rebuilding musl or GCC.
2. ~~Shell launches with no environment~~ — fixed: `sh` now starts programs
   with `posix_spawn`, passing the resolved path as argv[0] and an environment
   whose `PATH` is the shell's search path (`export PATH=` reaches children).
   Pipeline fds are close-on-exec so stages still inherit only stdio.
3. ~~Output file mode on ext2~~ , fixed: the VFS has a `setmode` inode
   operation (ext2 implements it), and `chmod`/`fchmod`/`fchmodat`/`umask`
   sit on it. A GCC-linked program on an ext2 root runs (verified with the
   cache limit below temporarily raised). libgaia's `open()` and
   `mkdir_path()` now pass 0666 / 0777 instead of leaving the mode register
   undefined.
4. Missing calls the run did not need yet but larger builds may:
   `getrusage` (as/ld tolerate it), `rename`, `getrandom`, `clock_nanosleep`,
   `set_robust_list`; `mremap` refused; no real `fork`.
5. ~~ext2 volumes above 128 MiB do not mount~~ , fixed: a device mount no
   longer copies the volume into one kmalloc. It reserves a kernel address
   window the size of the volume (`0xFFFFB000_00000000`, 64 GiB per mount,
   one per VFS mount slot) and reads each 4 KiB page from the device the
   first time `ext2_bytes` covers it. The ~222 MiB toolchain image now
   mounts as root at 1 GiB of RAM and GCC builds and runs programs on it.
   Pages are never evicted (the driver keeps raw pointers into its image),
   so resident memory is every page ever touched, at most the volume size.
6. VM capacity: 256 MiB mmap arena, 64 reservation records (now merged),
   16 free-list holes; `mprotect` does not update records, so a page dropped
   by `MADV_DONTNEED` after an `mprotect` refaults with its original
   permission. `PROT_NONE` reservations are mapped user-readable (mallocng
   depends on PROT_NONE → mprotect working, so fixing it needs `uvm_protect`
   to split records first).
