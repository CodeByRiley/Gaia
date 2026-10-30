#include "exec.h"
#include "console.h"
#include "path.h"

#include <lib/process.h>
#include <lib/syscall.h>

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* Resolve argv[0] as an ELF path and run it.
 *
 *   - Searches a small PATH for bare names.
 *   - Preserves explicit directory components.
 *   - Tries ".elf", then ".exe", when the final component has no extension.
 *   - Probes existence via open() so typos surface as a clean error
 *     rather than the kernel's "[x.elf exited -1]" message.
 *
 * When `bg != 0` the child is not waited for, so windowed apps don't pin
 * the prompt. The trailing `&` argv token sets bg upstream.
 *
 * The buffers are static and shared with spawn_argv. That is safe because
 * execve copies path and argv before posix_spawn returns. */
static char *prepare_argv(int argc, char **argv, char ***child_out) {
  if (argc < 1 || !argv[0] || !argv[0][0])
    return 0;
  static char fixed[256];
  if (process_resolve(argv[0], shell_exec_path(), fixed, sizeof(fixed)) != 0)
    return 0;

  /* argv[0] is the resolved path. GCC derives its install prefix from
   * argv[0] and, handed a bare "gcc", looked for cc1 relative to the
   * current directory. The kernel names the process from the path, so ps
   * is unaffected. */
  static char *child_argv[16];
  int n           = 0;
  child_argv[n++] = fixed;
  for (int j = 1; j < argc && n < 15; j++) {
    child_argv[n++] = argv[j];
  }
  child_argv[n] = 0;

  *child_out = child_argv;
  return fixed;
}

/* The environment every child starts with: the shell's own, with PATH
 * replaced by the shell's search path, so `export PATH=...` reaches the
 * programs it launches. Without one a child has nothing to search, and
 * GCC cannot find `as` or `ld`. Rebuilt per launch since PATH can change. */
#define CHILD_ENV_MAX 64
static char **child_environment(void) {
  static char path_var[sizeof("PATH=") + EXEC_PATH_MAX];
  static char *env[CHILD_ENV_MAX + 1];
  int n = 0;
  snprintf(path_var, sizeof(path_var), "PATH=%s", shell_exec_path());
  env[n++] = path_var;
  for (char **e = environ; e && *e && n < CHILD_ENV_MAX; e++)
    if (strncmp(*e, "PATH=", 5) != 0)
      env[n++] = *e;
  env[n] = 0;
  return env;
}

/* Start `path` with the shell's environment. Returns the pid, or -1 with
 * the reason printed. The child inherits every descriptor not marked
 * close-on-exec, which is why the pipeline's private ones carry it. */
static long launch(const char *path, char **child_argv) {
  pid_t pid;
  int rc = posix_spawn(&pid, path, 0, 0, child_argv, child_environment());
  if (rc != 0) {
    printf("%s: %s\n", path, strerror(rc));
    return -1;
  }
  return pid;
}

/* Start argv without waiting. Returns the pid, or -1 when the command is
 * not found or the spawn fails. */
static long spawn_argv(int argc, char **argv) {
  char **child_argv;
  char *fixed = prepare_argv(argc, argv, &child_argv);
  if (!fixed)
    return -1;
  return launch(fixed, child_argv);
}

int exec_argv(int argc, char **argv, int bg) {
  char **child_argv;
  char *fixed = prepare_argv(argc, argv, &child_argv);
  if (!fixed)
    return -1;

  long pid = launch(fixed, child_argv);
  if (pid < 0)
    return 0; /* found but not runnable; launch said why */
  if (bg) {
    printf("[%s pid=%d &]\n", fixed, (int)pid);
    return 0;
  }
  int status;
  while (waitpid((pid_t)pid, &status, 0) < 0 && errno == EINTR)
    ;
  return 0;
}

#define PIPELINE_MAX 8

/* `a | b | c`: one pipe between each pair of stages. Each stage is wired
 * the spawn-only way , point our own fd 0 and fd 1 at the stage's ends,
 * spawn it, move on. Children inherit every descriptor without FD_CLOEXEC,
 * so the saved stdio copies and the pipe ends are opened close-on-exec;
 * dup2 onto 0 and 1 clears the flag on the copies a stage should have. Our own
 * stdio is saved first and put back before anything is printed, so an
 * error never lands in a pipe. Every stage runs concurrently; we wait for
 * all of them. Built-ins cannot be stages: they run in this process. */
int run_pipeline(int ac, char **targs) {
  int start[PIPELINE_MAX], count[PIPELINE_MAX];
  int stages = 0;
  int from   = 0;
  for (int i = 0; i <= ac; i++) {
    if (i < ac && strcmp(targs[i], "|") != 0)
      continue;
    if (i == from || stages == PIPELINE_MAX) {
      printf(i == from ? "sh: syntax error near '|'\n" : "sh: at most %d pipeline stages\n",
             PIPELINE_MAX);
      return 1;
    }
    start[stages] = from;
    count[stages] = i - from;
    stages++;
    from = i + 1;
  }

  fflush(stdout);
  int saved_in  = fcntl(0, F_DUPFD_CLOEXEC, 3);
  int saved_out = fcntl(1, F_DUPFD_CLOEXEC, 3);
  if (saved_in < 0 || saved_out < 0) {
    printf("sh: cannot save stdio\n");
    return 1;
  }

  long pids[PIPELINE_MAX];
  for (int s = 0; s < PIPELINE_MAX; s++)
    pids[s] = -1;
  int missing   = -1;
  int prev_read = -1;
  for (int s = 0; s < stages; s++) {
    int fds[2] = {-1, -1};
    int last   = s == stages - 1;
    if (!last && pipe2(fds, O_CLOEXEC) != 0) {
      pids[s] = -1;
      break;
    }

    dup2(prev_read >= 0 ? prev_read : saved_in, 0);
    dup2(last ? saved_out : fds[1], 1);
    pids[s] = spawn_argv(count[s], targs + start[s]);
    if (pids[s] < 0 && missing < 0)
      missing = s;

    /* Our copies of the ends the child now holds. Keeping the write end
     * would stop the next stage ever seeing EOF. */
    if (prev_read >= 0)
      close(prev_read);
    if (!last) {
      close(fds[1]);
      prev_read = fds[0];
    }
  }
  if (prev_read >= 0)
    close(prev_read);

  dup2(saved_in, 0);
  dup2(saved_out, 1);
  close(saved_in);
  close(saved_out);

  if (missing >= 0)
    printf("%s: command not found\n", targs[start[missing]]);

  for (int s = 0; s < stages; s++) {
    int status;
    if (pids[s] > 0)
      waitpid((pid_t)pids[s], &status, 0);
  }
  return missing >= 0;
}
