/* userspace/libc/tests/syscall_compat.c , Linux syscall compatibility checks.
 *
 * A plain musl program: every check goes through the libc entry point a
 * port would use, and the raw syscall() form where the errno boundary itself
 * is the thing under test. It also builds and runs on a Linux host, which is
 * how the expected answers were confirmed:
 *
 *     cc -static -DSCRATCH='"/tmp/sctest"' -o sc syscall_compat.c
 *     sudo ./sc "$PWD/sc"
 *
 * (root, because the credential checks expect Gaia's single uid 0; the
 * absolute path argument is how the program finds itself to re-exec).
 *
 * Output is one line per check, "syscall-compat: PASS <name>" or
 * "syscall-compat: FAIL <name> ...", then a summary line the QEMU harness
 * (tests/syscall_compat_test.py) waits for. The program re-executes itself
 * with "child ..." arguments for the process checks.
 *
 * SCRATCH must be on a writable filesystem.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SCRATCH
#define SCRATCH "/sctest"
#endif

extern char **environ;

static int passed, failed;
static const char *self = "/usr/bin/muslsyscall.elf";

static void check(int ok, const char *name) {
  if (ok) {
    passed++;
    printf("syscall-compat: PASS %s\n", name);
  } else {
    failed++;
    printf("syscall-compat: FAIL %s errno=%d (%s)\n", name, errno, strerror(errno));
  }
}

/* rc == -1 with exactly this errno. */
static int fails_with(long rc, int expected) { return rc == -1 && errno == expected; }

static int write_file(const char *path, const char *text) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return -1;
  ssize_t n = write(fd, text, strlen(text));
  close(fd);
  return n == (ssize_t)strlen(text) ? 0 : -1;
}

/* ------------------------------------------------------------------ child */

static int child_main(int argc, char **argv) {
  if (argc < 3)
    return 100;
  const char *mode = argv[2];
  if (!strcmp(mode, "exit"))
    return argc > 3 ? atoi(argv[3]) : 0;
  if (!strcmp(mode, "env")) {
    const char *v = getenv("SCTEST");
    return v && !strcmp(v, "hello") && argc == 4 && !strcmp(argv[3], "arg with spaces") ? 0 : 1;
  }
  if (!strcmp(mode, "say")) {
    const char *msg = "child-out\n";
    return write(1, msg, strlen(msg)) == (ssize_t)strlen(msg) ? 0 : 1;
  }
  if (!strcmp(mode, "fdopen")) {
    /* argv[3] must be open, argv[4] must have been closed by exec. */
    int keep = atoi(argv[3]), gone = atoi(argv[4]);
    if (fcntl(keep, F_GETFD) < 0)
      return 2;
    if (fcntl(gone, F_GETFD) != -1 || errno != EBADF)
      return 3;
    return 0;
  }
  if (!strcmp(mode, "cwd")) {
    char buf[256];
    return getcwd(buf, sizeof(buf)) && !strcmp(buf, SCRATCH) ? 0 : 1;
  }
  return 101;
}

static int spawn_wait(char *const argv[], char *const envp[], posix_spawn_file_actions_t *fa) {
  pid_t pid;
  int rc = posix_spawn(&pid, self, fa, NULL, argv, envp ? envp : environ);
  if (rc != 0) {
    errno = rc;
    return -1;
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid)
    return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* ------------------------------------------------------------- filesystem */

static void test_access(void) {
  struct stat st;
  check(access("/", F_OK) == 0, "access-root-exists");
  check(access("/", R_OK | X_OK) == 0, "access-root-rx");
  check(fails_with(access("/no/such/path", F_OK), ENOENT), "access-missing-enoent");
  check(fails_with(access("/", 0x10), EINVAL), "access-bad-mode-einval");
  check(fails_with(syscall(SYS_access, (const char *)8, F_OK), EFAULT), "access-bad-pointer-efault");
  check(fails_with(access("", F_OK), ENOENT), "access-empty-enoent");
  check(access(self, X_OK) == 0, "access-self-executable");

  check(write_file(SCRATCH "/data.txt", "hello world\n") == 0, "create-data-file");
  if (stat(SCRATCH "/data.txt", &st) == 0) {
    int expect_x = (st.st_mode & 0111) != 0;
    int rc       = access(SCRATCH "/data.txt", X_OK);
    check(expect_x ? rc == 0 : fails_with(rc, EACCES), "access-x-follows-mode");
  } else {
    check(0, "access-x-follows-mode");
  }
  check(access(SCRATCH "/data.txt", R_OK | W_OK) == 0, "access-data-rw");
  check(fails_with(access(SCRATCH "/data.txt/x", F_OK), ENOTDIR) ||
            fails_with(access(SCRATCH "/data.txt/x", F_OK), ENOENT),
        "access-through-file");

  int dir = open(SCRATCH, O_RDONLY | O_DIRECTORY);
  check(dir >= 0, "open-dirfd");
  check(faccessat(dir, "data.txt", R_OK, 0) == 0, "faccessat-relative");
  check(faccessat(dir, "data.txt", R_OK, AT_EACCESS) == 0, "faccessat2-eaccess");
  check(fails_with(faccessat(dir, "missing", F_OK, 0), ENOENT), "faccessat-missing");
  check(faccessat(AT_FDCWD, "/", F_OK, 0) == 0, "faccessat-fdcwd");
  check(fails_with(faccessat(dir, "data.txt", F_OK, 0x8000), EINVAL), "faccessat2-bad-flag");
  int file = open(SCRATCH "/data.txt", O_RDONLY);
  check(fails_with(faccessat(file, "x", F_OK, 0), ENOTDIR), "faccessat-file-dirfd-enotdir");
  check(fails_with(faccessat(77, "x", F_OK, 0), EBADF), "faccessat-bad-dirfd-ebadf");
  check(faccessat(file, "/", F_OK, 0) == 0, "faccessat-absolute-ignores-dirfd");
  close(file);
  close(dir);
}

static void test_at_calls(void) {
  int dir = open(SCRATCH, O_RDONLY | O_DIRECTORY);
  struct stat a, b;

  int fd = openat(dir, "data.txt", O_RDONLY);
  check(fd >= 0, "openat-relative");
  char buf[32] = {0};
  check(fd >= 0 && read(fd, buf, 5) == 5 && !memcmp(buf, "hello", 5), "openat-read");

  check(fstatat(dir, "data.txt", &a, 0) == 0 && S_ISREG(a.st_mode) && a.st_size == 12,
        "newfstatat-relative");
  check(fd >= 0 && fstatat(fd, "", &b, AT_EMPTY_PATH) == 0 && b.st_size == 12,
        "newfstatat-empty-path");
  check(fails_with(fstatat(dir, "", &b, 0), ENOENT), "newfstatat-empty-no-flag");
  check(lstat(SCRATCH "/data.txt", &b) == 0 && b.st_size == a.st_size, "lstat-equals-stat");
  if (fd >= 0)
    close(fd);

  check(mkdirat(dir, "sub", 0755) == 0, "mkdirat");
  check(stat(SCRATCH "/sub", &a) == 0 && S_ISDIR(a.st_mode), "mkdirat-is-dir");
  check(fails_with(mkdirat(dir, "sub", 0755), EEXIST), "mkdirat-exists");
  check(write_file(SCRATCH "/sub/f", "x") == 0, "create-in-subdir");
  check(fails_with(unlinkat(dir, "sub", AT_REMOVEDIR), ENOTEMPTY), "unlinkat-nonempty");
  check(unlinkat(dir, "sub/f", 0) == 0, "unlinkat-file");
  check(unlinkat(dir, "sub", AT_REMOVEDIR) == 0, "unlinkat-removedir");
  check(fails_with(stat(SCRATCH "/sub", &a), ENOENT), "unlinkat-gone");
  check(fails_with(unlinkat(dir, "data.txt", 0x4), EINVAL), "unlinkat-bad-flag");

  char link[64];
  check(fails_with(readlink(SCRATCH "/data.txt", link, sizeof(link)), EINVAL),
        "readlink-regular-einval");
  check(fails_with(readlink("/no/such", link, sizeof(link)), ENOENT), "readlink-missing-enoent");
  check(fails_with(readlinkat(dir, "data.txt", link, sizeof(link)), EINVAL), "readlinkat-relative");
  char *real = realpath(SCRATCH "/./data.txt", NULL);
  check(real && !strcmp(real, SCRATCH "/data.txt"), "realpath");
  free(real);

  check(fchdir(dir) == 0, "fchdir");
  char cwd[256];
  check(getcwd(cwd, sizeof(cwd)) && !strcmp(cwd, SCRATCH), "fchdir-getcwd");
  check(access("data.txt", R_OK) == 0, "access-relative-cwd");
  check(chdir("/") == 0, "chdir-root");
  close(dir);

  int found = 0;
  DIR *d    = opendir(SCRATCH);
  struct dirent *e;
  while (d && (e = readdir(d)))
    if (!strcmp(e->d_name, "data.txt"))
      found = 1;
  if (d)
    closedir(d);
  check(found, "getdents64-finds-file");
}

static void test_fds(void) {
  int fd = open(SCRATCH "/data.txt", O_RDWR);
  check(fd >= 0, "open-rdwr");

  char buf[16] = {0};
  check(pread(fd, buf, 5, 6) == 5 && !memcmp(buf, "world", 5), "pread");
  check(lseek(fd, 0, SEEK_CUR) == 0, "pread-keeps-offset");
  check(pwrite(fd, "WORLD", 5, 6) == 5, "pwrite");
  check(lseek(fd, 0, SEEK_CUR) == 0, "pwrite-keeps-offset");
  memset(buf, 0, sizeof(buf));
  check(read(fd, buf, 12) == 12 && !memcmp(buf, "hello WORLD\n", 12), "pwrite-visible");
  check(pread(fd, buf, 4, 100) == 0, "pread-past-eof");
  check(fails_with(pread(fd, buf, 4, -1), EINVAL), "pread-negative-offset");

  /* A seek past EOF must hold, and the write after it must leave a zeroed
   * gap: BFD (as, ld) writes its section headers exactly this way. */
  int sparse = open(SCRATCH "/sparse.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
  struct stat sst;
  char gap[8];
  check(sparse >= 0 && write(sparse, "ab", 2) == 2 && lseek(sparse, 100, SEEK_SET) == 100 &&
            lseek(sparse, 0, SEEK_CUR) == 100,
        "lseek-past-eof-holds");
  check(sparse >= 0 && write(sparse, "Z", 1) == 1 && fstat(sparse, &sst) == 0 && sst.st_size == 101,
        "write-past-eof-extends");
  check(sparse >= 0 && pread(sparse, gap, 8, 50) == 8 && !memcmp(gap, "\0\0\0\0\0\0\0\0", 8) &&
            pread(sparse, gap, 1, 100) == 1 && gap[0] == 'Z',
        "write-past-eof-zero-gap");
  if (sparse >= 0)
    close(sparse);
  unlink(SCRATCH "/sparse.bin");

  check(fcntl(fd, F_GETFD) == 0, "fd-starts-without-cloexec");
  check(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 && fcntl(fd, F_GETFD) == FD_CLOEXEC,
        "fcntl-setfd-cloexec");
  int d = dup(fd);
  check(d >= 0 && fcntl(d, F_GETFD) == 0, "dup-clears-cloexec");
  check(d >= 0 && pread(d, buf, 5, 0) == 5 && !memcmp(buf, "hello", 5), "dup-shares-file");
  int d3 = dup3(fd, 20, O_CLOEXEC);
  check(d3 == 20 && fcntl(20, F_GETFD) == FD_CLOEXEC, "dup3-cloexec");
  check(fails_with(dup3(fd, fd, 0), EINVAL), "dup3-same-fd-einval");
  check(fails_with(dup3(fd, 21, 0x1), EINVAL), "dup3-bad-flags-einval");
  check(dup2(fd, fd) == fd, "dup2-same-fd");
  check(dup2(fd, 22) == 22 && fcntl(22, F_GETFD) == 0, "dup2-no-cloexec");
  int dc = fcntl(fd, F_DUPFD_CLOEXEC, 10);
  check(dc >= 10 && fcntl(dc, F_GETFD) == FD_CLOEXEC, "fcntl-dupfd-cloexec");
  close(d);
  close(20);
  close(22);
  close(dc);
  check(fails_with(fcntl(20, F_GETFD), EBADF), "close-releases");
  check(fails_with(close(20), EBADF), "close-twice-ebadf");
  close(fd);

  int p[2];
  check(pipe2(p, O_CLOEXEC) == 0, "pipe2-cloexec");
  check(fcntl(p[0], F_GETFD) == FD_CLOEXEC && fcntl(p[1], F_GETFD) == FD_CLOEXEC,
        "pipe2-flags-set");
  check(fails_with(pread(p[0], buf, 1, 0), ESPIPE), "pread-pipe-espipe");
  check(write(p[1], "ab", 2) == 2, "pipe-write");
  close(p[1]);
  check(read(p[0], buf, sizeof(buf)) == 2, "pipe-read");
  check(read(p[0], buf, sizeof(buf)) == 0, "pipe-eof");
  close(p[0]);
  check(fails_with(pipe2(p, 0x10), EINVAL), "pipe2-bad-flags");
}

/* chmod and umask. On a filesystem that stores modes (ext2, Linux) the
 * round trips must be exact; one that stores none (FAT) must say EPERM
 * rather than pretend. */
static void test_modes(void) {
  const char *path = SCRATCH "/mode.txt";
  struct stat st;
  check(write_file(path, "m") == 0, "mode-file-create");
  int rc = chmod(path, 0751);
  int stores_modes = rc == 0;
  check(rc == 0 || errno == EPERM, "chmod-done-or-eperm");
  if (stores_modes) {
    check(stat(path, &st) == 0 && (st.st_mode & 07777) == 0751 && S_ISREG(st.st_mode),
          "chmod-sets-permissions-keeps-type");
    check(access(path, X_OK) == 0, "chmod-x-grants-exec");
    check(chmod(path, 0644) == 0 && access(path, X_OK) == -1 && errno == EACCES,
          "chmod-clears-exec");
    int fd = open(path, O_RDONLY);
    check(fd >= 0 && fchmod(fd, 0700) == 0 && fstat(fd, &st) == 0 && (st.st_mode & 0777) == 0700,
          "fchmod");
    if (fd >= 0)
      close(fd);
    int dir = open(SCRATCH, O_RDONLY | O_DIRECTORY);
    check(fchmodat(dir, "mode.txt", 0640, 0) == 0 && stat(path, &st) == 0 &&
              (st.st_mode & 0777) == 0640,
          "fchmodat-relative");
    close(dir);
  }
  check(fails_with(chmod(SCRATCH "/missing", 0644), ENOENT), "chmod-missing-enoent");

  mode_t old = umask(027);
  check(umask(027) == 027, "umask-returns-previous");
  unlink(path);
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
  check(fd >= 0, "create-under-umask");
  if (fd >= 0)
    close(fd);
  if (stores_modes)
    check(stat(path, &st) == 0 && (st.st_mode & 0777) == 0640, "create-mode-minus-umask");
  check(mkdir(SCRATCH "/mdir", 0777) == 0, "mkdir-under-umask");
  if (stores_modes)
    check(stat(SCRATCH "/mdir", &st) == 0 && (st.st_mode & 0777) == 0750 && S_ISDIR(st.st_mode),
          "mkdir-mode-minus-umask");
  rmdir(SCRATCH "/mdir");
  umask(old);
  unlink(path);
}

static void test_ids(void) {
  check(getuid() == 0 && geteuid() == 0 && getgid() == 0 && getegid() == 0, "ids-are-root");
  check(getppid() > 0, "getppid");
  check(getpid() > 0, "getpid");
}

/* ----------------------------------------------------------------- memory */

static void test_memory(void) {
  size_t len = 64 * 4096;
  unsigned char *m =
      mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  check(m != MAP_FAILED, "mmap-anon");
  if (m != MAP_FAILED) {
    int zero = 1;
    for (size_t i = 0; i < len; i += 4096)
      zero &= m[i] == 0 && m[i + 4095] == 0;
    check(zero, "mmap-zeroed");
    memset(m, 0xab, len);
    check(madvise(m, len, MADV_WILLNEED) == 0, "madvise-willneed");
    check(m[0] == 0xab, "madvise-hint-keeps-data");
    check(madvise(m, len, MADV_DONTNEED) == 0, "madvise-dontneed");
    check(m[0] == 0 && m[len - 1] == 0, "madvise-dontneed-zeroes");
    check(fails_with(madvise(m, len, 9999), EINVAL), "madvise-bad-advice");
    check(fails_with(madvise(m + 1, 4096, MADV_NORMAL), EINVAL), "madvise-unaligned");
    check(mprotect(m, 4096, PROT_READ) == 0, "mprotect-ro");
    check(munmap(m, len) == 0, "munmap");
  }
  check(fails_with((long)(intptr_t)mmap(NULL, 0, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0),
                   EINVAL),
        "mmap-zero-len");

  int fd = open(SCRATCH "/data.txt", O_RDONLY);
  char *f = fd >= 0 ? mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
  check(f != MAP_FAILED && !memcmp(f, "hello WORLD\n", 12) && f[12] == 0, "mmap-file-private");
  check(fd >= 0 && lseek(fd, 0, SEEK_CUR) == 0, "mmap-file-keeps-offset");
  if (f != MAP_FAILED)
    munmap(f, 4096);
  /* Linux maps it; Gaia has no page cache to share and must say so rather
   * than hand back a private copy. */
  char *s = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
  check(s == MAP_FAILED ? errno == ENODEV : !memcmp(s, "hello", 5), "mmap-file-shared-honest");
  if (s != MAP_FAILED)
    munmap(s, 4096);
  check(fails_with((long)(intptr_t)mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 100), EINVAL),
        "mmap-unaligned-offset");
  check(fails_with((long)(intptr_t)mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, 29, 0), EBADF),
        "mmap-bad-fd");
  close(fd);

  size_t big = 48u << 20;
  unsigned char *h = malloc(big);
  check(h != NULL, "malloc-48m");
  if (h) {
    for (size_t i = 0; i < big; i += 1 << 20)
      h[i] = (unsigned char)i;
    int ok = 1;
    for (size_t i = 0; i < big; i += 1 << 20)
      ok &= h[i] == (unsigned char)i;
    check(ok, "malloc-48m-touch");
    free(h);
  }
}

/* ---------------------------------------------------------------- process */

static void test_process(void) {
  char *exit7[] = {"muslsyscall", "child", "exit", "7", NULL};
  check(spawn_wait(exit7, NULL, NULL) == 7, "posix-spawn-exit-code");

  char *env_argv[] = {"muslsyscall", "child", "env", "arg with spaces", NULL};
  char *env[]      = {"SCTEST=hello", "OTHER=1", NULL};
  check(spawn_wait(env_argv, env, NULL) == 0, "posix-spawn-argv-env");

  int ok = 1;
  for (int i = 0; i < 5; i++) {
    char code[4];
    snprintf(code, sizeof(code), "%d", i + 1);
    char *argv[] = {"muslsyscall", "child", "exit", code, NULL};
    ok &= spawn_wait(argv, NULL, NULL) == i + 1;
  }
  check(ok, "posix-spawn-sequential");

  int p[2];
  if (pipe(p) == 0) {
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, p[1], 1);
    posix_spawn_file_actions_addclose(&fa, p[0]);
    char *say[] = {"muslsyscall", "child", "say", NULL};
    pid_t pid;
    int rc = posix_spawn(&pid, self, &fa, NULL, say, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(p[1]);
    char buf[32] = {0};
    ssize_t n    = rc == 0 ? read(p[0], buf, sizeof(buf) - 1) : -1;
    int status   = 0;
    if (rc == 0)
      waitpid(pid, &status, 0);
    close(p[0]);
    check(rc == 0 && n == 10 && !strcmp(buf, "child-out\n") && WIFEXITED(status) &&
              WEXITSTATUS(status) == 0,
          "posix-spawn-stdout-pipe");
  } else {
    check(0, "posix-spawn-stdout-pipe");
  }

  int keep = open(SCRATCH "/data.txt", O_RDONLY);
  int gone = open(SCRATCH "/data.txt", O_RDONLY | O_CLOEXEC);
  char ks[8], gs[8];
  snprintf(ks, sizeof(ks), "%d", keep);
  snprintf(gs, sizeof(gs), "%d", gone);
  char *fdargv[] = {"muslsyscall", "child", "fdopen", ks, gs, NULL};
  check(spawn_wait(fdargv, NULL, NULL) == 0, "exec-closes-cloexec-only");
  close(keep);
  close(gone);

  check(chdir(SCRATCH) == 0, "chdir-scratch");
  char *cwdargv[] = {"muslsyscall", "child", "cwd", NULL};
  check(spawn_wait(cwdargv, NULL, NULL) == 0, "exec-keeps-cwd");
  check(chdir("/") == 0, "chdir-back");

  pid_t pid;
  char *none[] = {"nope", NULL};
  int rc       = posix_spawn(&pid, "/no/such/program", NULL, NULL, none, environ);
  check(rc == ENOENT, "posix-spawn-missing-enoent");
  rc = posix_spawn(&pid, SCRATCH "/data.txt", NULL, NULL, none, environ);
  check(rc == EACCES || rc == ENOEXEC, "posix-spawn-nonexec-fails");
  check(fails_with(waitpid(-1, NULL, WNOHANG), ECHILD), "failed-spawn-leaves-no-child");

  pid = vfork();
  if (pid == 0) {
    char *argv[] = {"muslsyscall", "child", "exit", "3", NULL};
    execve(self, argv, environ);
    _exit(99);
  }
  int status = 0;
  check(pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
            WEXITSTATUS(status) == 3,
        "vfork-execve");

  pid = vfork();
  if (pid == 0)
    _exit(42);
  check(pid > 0 && waitpid(pid, &status, 0) == pid && WEXITSTATUS(status) == 42,
        "vfork-exit-without-exec");

  check(fails_with(execve("/no/such/program", none, environ), ENOENT), "execve-missing-enoent");
  check(fails_with(execve(SCRATCH, none, environ), EACCES), "execve-directory-eacces");
  check(fails_with(syscall(SYS_execve, self, (char **)8, environ), EFAULT), "execve-bad-argv");
}

/* `muslsyscall run [NAME=VALUE...] PROGRAM [ARG...]`: exec PROGRAM with the
 * caller's environment plus the assignments, like env(1). Gaia's shell
 * starts programs with an empty environment, and this is how a test hands
 * one a PATH through the real execve path. */
static int run_main(int argc, char **argv) {
  static char *env[64];
  int envc = 0, i = 2;
  for (char **e = environ; *e && envc < 48; e++)
    env[envc++] = *e;
  for (; i < argc && strchr(argv[i], '=') && envc < 63; i++)
    env[envc++] = argv[i];
  env[envc] = NULL;
  if (i >= argc)
    return 125;
  execve(argv[i], argv + i, env);
  printf("run: execve %s: %s\n", argv[i], strerror(errno));
  return 127;
}

static void test_limits(void) {
  struct rlimit r;
  check(getrlimit(RLIMIT_STACK, &r) == 0 && r.rlim_cur > 0 && r.rlim_cur <= r.rlim_max,
        "getrlimit-stack");
  check(getrlimit(RLIMIT_NOFILE, &r) == 0 && r.rlim_cur >= 20, "getrlimit-nofile");
  check(getrlimit(RLIMIT_STACK, &r) == 0 && setrlimit(RLIMIT_STACK, &r) == 0,
        "setrlimit-unchanged");
  check(fails_with(getrlimit(99, &r), EINVAL), "getrlimit-bad-resource");
}

int main(int argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "child"))
    return child_main(argc, argv);
  if (argc >= 2 && !strcmp(argv[1], "run"))
    return run_main(argc, argv);
  if (argc >= 2 && argv[1][0] == '/')
    self = argv[1];

  setvbuf(stdout, NULL, _IONBF, 0);
  printf("syscall-compat: start\n");
  mkdir(SCRATCH, 0755);
  unlink(SCRATCH "/data.txt");

  test_access();
  test_at_calls();
  test_fds();
  test_modes();
  test_ids();
  test_limits();
  test_memory();
  test_process();

  unlink(SCRATCH "/data.txt");
  rmdir(SCRATCH);
  printf("syscall-compat: DONE pass=%d fail=%d\n", passed, failed);
  return failed ? 1 : 0;
}
