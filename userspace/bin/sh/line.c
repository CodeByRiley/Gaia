#include "line.h"
#include "complete.h"
#include "console.h"
#include "history.h"
#include "input.h"
#include "lib/console.h"

#include <string.h>

/* Swap the line being edited for `text`, on screen and in `buf`: erase the
 * typed characters the way Backspace does, then echo the replacement. The
 * cursor is always at the end of the line, so erasing *n characters clears
 * exactly what was typed. `text` is truncated to fit `max`. */
static void replace_line(char *buf, int *n, int max, const char *text) {
  for (; *n > 0; (*n)--)
    console_puts("\b \b");
  for (; text[*n] && *n + 1 < max; (*n)++) {
    buf[*n] = text[*n];
    console_putc(text[*n]);
  }
  buf[*n] = '\0';
}

int read_line(char *buf, int max) {
  int n                     = 0;
  int last_was_tab          = 0;
  int history_pos           = history_count();
  char saved_line[LINE_MAX] = {0};
  buf[0]                    = '\0';
  while (1) {
    input_event_t event = read_key();
    switch (event.type) {
    case INPUT_CHAR: {
      last_was_tab = 0;

      if (n + 1 < max) {
        buf[n++] = event.character;
        console_putc(event.character);
      }
      break;
    }

    case INPUT_ENTER:
      console_putc('\n');
      buf[n] = '\0';
      return n;

    case INPUT_BACKSPACE:
      last_was_tab = 0;

      if (n > 0) {
        n--;
        console_puts("\b \b");
      }
      break;

    case INPUT_TAB:
      handle_tab(buf, &n, max, last_was_tab);
      last_was_tab = 1;
      break;

    case INPUT_UP: {
      last_was_tab = 0;

      int count = history_count();

      if (count == 0 || history_pos == 0)
        break;

      // Preserve the unfinished command on the first Up.
      if (history_pos == count) {
        memcpy(saved_line, buf, n);
        saved_line[n] = '\0';
      }

      const char *command = history_get(--history_pos);
      if (command)
        replace_line(buf, &n, max, command);

      break;
    }
    case INPUT_DOWN: {
      last_was_tab = 0;

      int count = history_count();

      if (history_pos >= count)
        break;

      if (++history_pos == count) {
        replace_line(buf, &n, max, saved_line);
      } else {
        const char *command = history_get(history_pos);
        if (command)
          replace_line(buf, &n, max, command);
      }

      break;
    }

    default:
      last_was_tab = 0;
      break;
    }
  }
}
