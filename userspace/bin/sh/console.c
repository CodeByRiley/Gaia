#include "console.h"

#include <lib/console.h>
#include <stdarg.h>
#include <stdio.h>

/* Local printf bound to the console (not stdout) so anything writing to
 * fd 1 elsewhere doesn't mix into the shell's UI. */
int sh_printf(const char *fmt, ...) {
  char buf[512];

  va_list ap;
  va_start(ap, fmt);
  int r = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (r < 0)
    return r;

  size_t written = (size_t)r;

  if (written >= sizeof(buf))
    written = sizeof(buf) - 1;

  console_write(buf, written);
  return r;
}
