#include "line.h"
#include "console.h"
#include "lib/console.h"
#include "complete.h"
#include "input.h"
/* Read a line into buf. Handles backspace + TAB locally; returns length
 * (excluding the trailing NUL). Echoes characters as it goes. */
int read_line(char *buf, int max) {
  int n            = 0;
  int last_was_tab = 0;
  while (1) {
    char c = read_char();
    if (c != '\t')
      last_was_tab = 0;
    if (c == '\n') {
      console_putc('\n');
      buf[n] = 0;
      return n;
    }
    if (c == '\b') {
      if (n > 0) {
        n--;
        console_puts("\b \b");
      }
      continue;
    }
    if (c == '\t') {
      handle_tab(buf, &n, max, last_was_tab);
      last_was_tab = 1;
      continue;
    }
    if (n + 1 < max) {
      buf[n++] = c;
      console_putc(c);
    }
  }
}
