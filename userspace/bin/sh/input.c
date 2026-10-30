#include "input.h"
#include "lib/syscall.h"

/* One byte of console input, blocking. Keystrokes come from the queue
 * heimdall injects for whoever owns the console (tty_read_input), never the
 * raw keyboard ring: that one receives every key regardless of focus, so the
 * shell would silently collect everything typed into other windows and find
 * it queued at the prompt the moment they closed.
 *
 * Characters arrive already folded to ASCII (heimdall applies the keymap), so
 * there is no modifier state to track here. */
static char read_char(void) {
  static char buf[32];
  static int have = 0;
  static int next = 0;

  while (1) {
    if (next < have)
      return buf[next++];

    long n = tty_read_input(buf, sizeof(buf));
    if (n > 0) {
      have = (int)n;
      next = 0;
      continue;
    }
    have = next = 0;
    sleep_ticks(1);
  }
}

input_event_t read_key(void) {
  unsigned char c = (unsigned char)read_char();

  if (c == '\x1b') {
    unsigned char next = (unsigned char)read_char();

    if (next == '[') {
      unsigned char code = (unsigned char)read_char();

      switch (code) {
      case 'A':
        return (input_event_t){INPUT_UP, 0};
      case 'B':
        return (input_event_t){INPUT_DOWN, 0};
      case 'C':
        return (input_event_t){INPUT_RIGHT, 0};
      case 'D':
        return (input_event_t){INPUT_LEFT, 0};
      case 'H':
        return (input_event_t){INPUT_HOME, 0};
      case 'F':
        return (input_event_t){INPUT_END, 0};
      }
    }
  }

  switch (c) {
  case '\r':
  case '\n':
    return (input_event_t){INPUT_ENTER, 0};
  case '\b':
    return (input_event_t){INPUT_BACKSPACE, 0};
  case '\t':
    return (input_event_t){INPUT_TAB, 0};
  default:
    return (input_event_t){INPUT_CHAR, (char)c};
  }
}
