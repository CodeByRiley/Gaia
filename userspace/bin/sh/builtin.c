#include "builtin.h"
#include "console.h"
#include "exec.h"
#include "path.h"

#include <lib/console.h>
#include <lib/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int shell_should_exit = 0;

static int builtin_help(int argc, char **argv);
static int builtin_mkdir(int argc, char **argv);
static int builtin_echo(int argc, char **argv);
static int builtin_write(int argc, char **argv);
static int builtin_rm(int argc, char **argv);
static int builtin_clear(int argc, char **argv);
static int builtin_run(int argc, char **argv);
static int builtin_test(int argc, char **argv);
static int builtin_fdnstest(int argc, char **argv);
static int builtin_exit(int argc, char **argv);
static int builtin_cd(int argc, char **argv);
static int builtin_pwd(int argc, char **argv);
static int builtin_export(int argc, char **argv);
static int builtin_fault(int argc, char **argv);

static const struct builtin builtins[] = {
    {"help",     "help",                        builtin_help    },
    {"clear",    "clear",                       builtin_clear   },
    {"exit",     "exit",                        builtin_exit    },
    {"run",      "run PATH[.elf] [ARG...] [&]", builtin_run     },
    {"mkdir",    "mkdir DIR",                   builtin_mkdir   },
    {"rm",       "rm FILE",                     builtin_rm      },
    {"echo",     "echo TEXT...",                builtin_echo    },
    {"write",    "write FILE TEXT...",          builtin_write   },
    {"test",     "test",                        builtin_test    },
    {"fdnstest", "fdnstest",                    builtin_fdnstest},
    {"fault",    "fault",                       builtin_fault   },
    {"cd",       "cd DIR",                      builtin_cd      },
    {"pwd",      "pwd",                         builtin_pwd     },
    {"export",   "export PATH=DIR[:DIR...]",    builtin_export  },
    {0,          0,                             0               },
};

const struct builtin *find_builtin(const char *name) {
  for (int i = 0; builtins[i].name; i++) {
    if (strcmp(name, builtins[i].name) == 0)
      return &builtins[i];
  }

  return NULL;
}

int shell_exit_requested(void) { return shell_should_exit; }

/* `run PATH[.elf] [ARGS...] [&]` , explicit form. Strips the leading
 * "run" token and forwards the rest to exec_argv. */
static int builtin_run(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: run PATH[.elf] [ARG...] [&]\n");
    return 1;
  }
  int bg = 0;
  if (argc >= 2 && argv[argc - 1] && strcmp(argv[argc - 1], "&") == 0) {
    bg = 1;
    argc--;
  }
  return exec_argv(argc - 1, argv + 1, bg);
}

static int builtin_test(int argc, char **argv) {
  (void)argc;
  (void)argv;

  char a_buf[5] = {0};
  char b_buf[5] = {0};

  int a = (int)open("readme.txt", 0);
  int b = (int)open("readme.txt", 0);

  if (a < 0 || b < 0) {
    printf("fdtest: open failed a=%d b=%d\n", a, b);
    if (a >= 0)
      close(a);
    if (b >= 0)
      close(b);
    return 1;
  }

  long ar = read(a, a_buf, 4);
  long br = read(b, b_buf, 4);

  printf("fdtest: a='%s' b='%s'\n", a_buf, b_buf);

  if (ar == 4 && br == 4 && strcmp(a_buf, b_buf) == 0) {

    sh_printf("fdtest: PASS independent open offsets\n");
  } else {
    sh_printf("fdtest: FAIL ar=%d br=%d\n", (int)ar, (int)br);
  }

  close(a);
  close(b);
  return (ar == 4 && br == 4 && strcmp(a_buf, b_buf) == 0) ? 0 : 1;
}

static int builtin_fdnstest(int argc, char **argv) {
  (void)argc;
  (void)argv;

  int parent_fd = (int)open("readme.txt", 0);
  if (parent_fd < 0) {
    printf("fdnstest: parent open failed\n");
    return 1;
  }

  printf("fdnstest: parent fd=%d\n", parent_fd);

  char *child_argv[] = {"fdchild", 0};
  long code          = exec("/usr/bin/fdchild.elf", child_argv);

  printf("fdnstest: child exited %d\n", (int)code);
  close(parent_fd);
  return code == 0 ? 0 : 1;
}

static int builtin_help(int argc, char **argv) {
  (void)argc;
  (void)argv;

  printf("builtins:\n");
  for (int i = 0; builtins[i].name; i++)
    printf("  %s\n", builtins[i].usage);
  printf("external programs: type an ELF basename or path\n");
  return 0;
}

static int builtin_mkdir(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: mkdir DIR\n");
    return 1;
  }
  if (mkdir_path(argv[1]) != 0)
    printf("mkdir: %s: failed\n", argv[1]);
  return 0;
}

static int builtin_echo(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (i > 1)
      console_putc(' ');
    console_puts(argv[i]);
  }
  console_putc('\n');
  return 0;
}

static int builtin_write(int argc, char **argv) {
  if (argc < 3) {
    printf("usage: write FILE TEXT...\n");
    return 1;
  }
  FILE *fp = fopen(argv[1], "w");
  if (!fp) {
    printf("write: %s: open failed\n", argv[1]);
    return 1;
  }
  for (int i = 2; i < argc; i++) {
    if (i > 2)
      fwrite(" ", 1, 1, fp);
    fwrite(argv[i], 1, strlen(argv[i]), fp);
  }
  fwrite("\n", 1, 1, fp);
  fclose(fp);
  return 0;
}

static int builtin_rm(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: rm FILE\n");
    return 1;
  }
  if (unlink(argv[1]) != 0) {
    printf("rm: %s: not found\n", argv[1]);
    return 1;
  }
  return 0;
}

static int builtin_clear(int argc, char **argv) {
  (void)argc;
  (void)argv;
  console_clear();
  return 0;
}

static int builtin_exit(int argc, char **argv) {
  (void)argc;
  (void)argv;
  shell_should_exit = 1;
  return 0;
}

static int builtin_cd(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "/";
  if (chdir(path) != 0) {
    printf("cd: %s: failed\n", path);
    return 1;
  }
  return 0;
}

static int builtin_pwd(int argc, char **argv) {
  (void)argc;
  (void)argv;

  char buf[256];
  if (!getcwd(buf, sizeof(buf))) {
    printf("pwd: failed\n");
    return 1;
  }

  printf("%s\n", buf);
  return 0;
}

static int builtin_export(int argc, char **argv) {
  if (argc == 1) {
    printf("PATH=%s\n", shell_exec_path());
    return 0;
  }
  if (argc != 2) {
    printf("usage: export PATH=DIR[:DIR...]\n");
    return 1;
  }
  int assignment = apply_path_assignment(argv[1]);
  if (assignment == 0) {
    printf("usage: export PATH=DIR[:DIR...]\n");
    return 1;
  }
  if (assignment < 0) {
    printf("export: PATH is too long\n");
    return 1;
  }
  return 0;
}

static int builtin_fault(int argc, char **argv) {
  (void)argc;
  (void)argv;

#ifndef DEBUG
  printf("fault: only available in debug builds\n");
  return 1;
#else
  printf("About to intentionally dereference a null pointer...\n");

  volatile int *bad_ptr = (volatile int *)0;
  *bad_ptr              = 0xDEADBEEF;
#endif

  return 0;
}
