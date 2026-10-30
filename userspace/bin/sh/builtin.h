#ifndef SHELL_INTERNAL_BUILTINS_H
#define SHELL_INTERNAL_BUILTINS_H

struct builtin {
  const char *name;
  const char *usage;
  int (*fn)(int argc, char **argv);
};

const struct builtin *find_builtin(const char *name);
int shell_exit_requested(void);

#endif
