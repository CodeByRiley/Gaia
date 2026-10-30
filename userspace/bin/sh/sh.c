/* userspace/bin/sh/sh.c , tsh: toy interactive shell.
 *
 * Reads a line, tokenises it, dispatches shell-owned built-ins through a
 * table, and falls through to a filesystem ELF lookup so `btop` runs
 * btop.elf without a `run` prefix.
 * Foreground execs block on exec(); a trailing `&` token launches via
 * spawn() so windowed apps don't pin the prompt.
 *
 * Input model:
 *   - ASCII drained from the TTY input ring, which heimdall fills only while
 *     the console window has focus. Not kbd_poll: that ring receives every
 *     keystroke regardless of focus, so a shell reading it also collects
 *     whatever is typed into other windows.
 *   - Modifiers and console zoom are heimdall's business, since the keymap is
 *     applied before the characters reach us.
 *   - TAB completes command names from PATH or files from the current dir.
 */
// #region INCLUDES
#include "builtin.h"
#include "console.h"
#include "exec.h"
#include "history.h"
#include "line.h"
#include "parse.h"
#include "path.h"
#include "shell.h"

#include <lib/console.h>
#include <stdio.h>
#include <string.h>
// #endregion INCLUDES

// #region MAIN
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  console_init();
  sh_printf("Skald v0.3 - type 'help'\n");

  char line[LINE_MAX];
  char *targs[16];

  while (1) {
    console_puts("$ ");
    int len = read_line(line, sizeof(line));
    if (len == 0)
      continue;

    history_add(line);

    int ac = tokenize(line, targs, 16);
    if (ac == 0)
      continue;
    char *cmd = targs[0];

    if (ac == 1) {
      int assignment = apply_path_assignment(cmd);
      if (assignment != 0) {
        if (assignment < 0)
          printf("PATH: value is too long\n");
        continue;
      }
    }

    int piped = 0;
    for (int i = 0; i < ac; i++)
      if (strcmp(targs[i], "|") == 0)
        piped = 1;
    if (piped) {
      run_pipeline(ac, targs);
      continue;
    }

    const struct builtin *builtin = find_builtin(cmd);
    if (builtin) {
      builtin->fn(ac, targs);
      if (shell_exit_requested()) {
        printf("exiting Skald\n");
        return 0;
      }
    } else {
      /* No built-in matched , try filesystem lookup. A trailing
       * `&` token means launch backgrounded via spawn(). */
      int bg     = 0;
      int eff_ac = ac;
      if (eff_ac >= 1 && targs[eff_ac - 1] && strcmp(targs[eff_ac - 1], "&") == 0) {
        bg = 1;
        eff_ac--;
      }
      if (exec_argv(eff_ac, targs, bg) != 0) {
        printf("%s: command not found\n", cmd);
      }
    }
  }
}
// #endregion MAIN
