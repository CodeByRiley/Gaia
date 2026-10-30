#include "parse.h"

/* Split `line` into argv-style tokens in place. Pointers in argv alias
 * into line, which is modified (NUL inserted at each separator). */
int tokenize(char *line, char **argv, int max) {
  int argc = 0;
  char *p  = line;
  while (*p && argc < max) {
    while (*p == ' ' || *p == '\t')
      p++;
    if (!*p)
      break;
    argv[argc++] = p;
    while (*p && *p != ' ' && *p != '\t')
      p++;
    if (*p) {
      *p = 0;
      p++;
    }
  }
  return argc;
}
