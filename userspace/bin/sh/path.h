#ifndef SKALD_PATH_H
#define SKALD_PATH_H

#define EXEC_PATH_MAX 256
#define EXEC_DIR_MAX 256

const char *shell_exec_path(void);

int path_next(const char *path, int *cursor, char *out, int max);
int apply_path_assignment(const char *word);

#endif
