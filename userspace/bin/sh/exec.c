#include "exec.h"
#include "path.h"
#include "console.h"

#include <lib/process.h>
#include <lib/syscall.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static const char *path_basename(const char *path) {
  const char *base = path;
  for (const char *p = path; *p; p++) {
    if (*p == '/' || *p == '\\')
      base = p + 1;
  }
  return base;
}

/* Resolve argv[0] as an ELF path and run it.
 *
 *   - Searches a small PATH for bare names.
 *   - Preserves explicit directory components.
 *   - Tries ".elf", then ".exe", when the final component has no extension.
 *   - Probes existence via open() so typos surface as a clean error
 *     rather than the kernel's "[x.elf exited -1]" message.
 *
 * When `bg != 0` the child is launched via spawn() (fire-and-forget) so
 * windowed apps don't pin the prompt. The trailing `&` argv token sets
 * bg upstream.
 *
 * The buffers are static and shared with spawn_argv. That is safe because
 * the kernel copies path and argv before spawn() or exec() returns. */
static char *prepare_argv(int argc, char **argv, char ***child_out) {
  if (argc < 1 || !argv[0] || !argv[0][0])
    return 0;
  static char fixed[256];
  if (process_resolve(argv[0], shell_exec_path(), fixed, sizeof(fixed)) != 0)
    return 0;

  /* Lowercase base (sans extension) for child's argv[0]. */
  const char *base = path_basename(fixed);
  static char prog_name[16];
  int pn = 0;
  while (pn < (int)sizeof(prog_name) - 1 && base[pn] && base[pn] != '.') {
    char c = base[pn];
    if (c >= 'A' && c <= 'Z')
      c += 32;
    prog_name[pn] = c;
    pn++;
  }
  prog_name[pn] = 0;

  static char *child_argv[16];
  int n           = 0;
  child_argv[n++] = prog_name;
  for (int j = 1; j < argc && n < 15; j++) {
    child_argv[n++] = argv[j];
  }
  child_argv[n] = 0;

  *child_out = child_argv;
  return fixed;
}

/* Start argv without waiting. Returns the pid, or -1 when the command is
 * not found or the spawn fails. */
static long spawn_argv(int argc, char **argv) {
  char **child_argv;
  char *fixed = prepare_argv(argc, argv, &child_argv);
  if (!fixed)
    return -1;
  return spawn(fixed, child_argv);
}

int exec_argv(int argc, char **argv, int bg) {
  char **child_argv;
  char *fixed = prepare_argv(argc, argv, &child_argv);
  if (!fixed)
    return -1;

  if (bg) {
    long pid = spawn(fixed, child_argv);
    if (pid < 0) {
      printf("%s: spawn failed\n", fixed);
      return -1;
    }
    printf("[%s pid=%d &]\n", fixed, (int)pid);
    return 0;
  }
  long code = exec(fixed, child_argv);
  // console_clear();
  // printf("[%s exited %d]\n", fixed, (int)code);
  return 0;
}

#define PIPELINE_MAX 8

/* `a | b | c`: one pipe between each pair of stages. There is no fork, so
 * each stage is wired the spawn-only way , point our own fd 0 and fd 1 at
 * the stage's ends, spawn it (children inherit fds 0-2), move on. Our own
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
  int saved_in  = dup(0);
  int saved_out = dup(1);
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
    if (!last && pipe(fds) != 0) {
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
