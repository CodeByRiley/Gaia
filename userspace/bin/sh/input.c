#include "input.h"
#include "lib/syscall.h"

char read_char(void) {
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
