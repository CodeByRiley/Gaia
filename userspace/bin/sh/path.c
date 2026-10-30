#include "path.h"
#include "shell.h"

#define DEFAULT_EXEC_PATH "/bin:/usr/bin:/usr/local/bin:/system/bin"

static char exec_path[EXEC_PATH_MAX] = DEFAULT_EXEC_PATH;

const char *shell_exec_path(void) { return exec_path; }

/* Read one colon-delimited PATH component. */
int path_next(const char *path, int *cursor, char *out, int max) {
  if (!path || !cursor || *cursor < 0 || !out || max < 2)
    return 0;

  int i        = *cursor;
  int n        = 0;
  int overflow = 0;

  while (path[i] && path[i] != ':') {
    if (n + 1 < max)
      out[n++] = path[i];
    else
      overflow = 1;

    i++;
  }

  *cursor = path[i] == ':' ? i + 1 : -1;

  if (n == 0)
    out[n++] = '.';

  out[n] = 0;
  return overflow ? -1 : 1;
}

static int set_exec_path(const char *value) {
  int n = 0;

  if (!value)
    return -1;

  while (value[n]) {
    if (n + 1 >= EXEC_PATH_MAX)
      return -1;
    n++;
  }

  for (int i = 0; i <= n; i++)
    exec_path[i] = value[i];

  return 0;
}

/* Returns 0 for a normal word, 1 for a valid PATH
 * assignment, and -1 for an assignment that is too long. */
int apply_path_assignment(const char *word) {
  static const char prefix[] = "PATH=";

  if (!word)
    return 0;

  for (int i = 0; prefix[i]; i++) {
    if (word[i] != prefix[i])
      return 0;
  }

  return set_exec_path(word + sizeof(prefix) - 1) == 0 ? 1 : -1;
}
