#include "complete.h"
#include "bin/sh/path.h"
#include "bin/sh/shell.h"
#include "lib/console.h"
#include "lib/syscall.h"
#include <string.h>

static char comp_matches[COMP_MAX_MATCHES][COMP_NAME_MAX];
static int comp_count    = 0;
static int comp_index    = 0;
static int comp_word_len = 0; /* length of word that was replaced */

/* Index of first character of the last whitespace-delimited word in
 * buf[0..n]. */
static int word_start(const char *buf, int n) {
  int i = n;
  while (i > 0 && buf[i - 1] != ' ' && buf[i - 1] != '\t')
    i--;
  return i;
}

static int is_command_word(const char *buf, int word_offset) {
  for (int i = 0; i < word_offset; i++) {
    if (buf[i] != ' ' && buf[i] != '\t')
      return 0;
  }
  return 1;
}

/* Case-insensitive prefix match. plen passed separately so prefix can be
 * a substring of buf without a NUL boundary. */
static int starts_with_ci(const char *name, const char *prefix, int plen) {
  for (int i = 0; i < plen; i++) {
    char a = name[i];
    char b = prefix[i];
    if (!a)
      return 0;
    if (a >= 'A' && a <= 'Z')
      a = (char)(a + 32);
    if (b >= 'A' && b <= 'Z')
      b = (char)(b + 32);
    if (a != b)
      return 0;
  }
  return 1;
}

static int ends_with_ci(const char *name, int length, const char *suffix) {
  int suffix_len = (int)strlen(suffix);
  if (length < suffix_len)
    return 0;
  for (int i = 0; i < suffix_len; i++) {
    char a = name[length - suffix_len + i];
    char b = suffix[i];
    if (a >= 'A' && a <= 'Z')
      a = (char)(a + 32);
    if (b >= 'A' && b <= 'Z')
      b = (char)(b + 32);
    if (a != b)
      return 0;
  }
  return 1;
}

static int comp_contains(const char *name, int length) {
  for (int i = 0; i < comp_count; i++) {
    if ((int)strlen(comp_matches[i]) != length)
      continue;
    int same = 1;
    for (int j = 0; j < length; j++) {
      if (comp_matches[i][j] != name[j]) {
        same = 0;
        break;
      }
    }
    if (same)
      return 1;
  }
  return 0;
}

static void comp_add(const char *name, int length, const char *prefix, int prefix_len) {
  if (comp_count >= COMP_MAX_MATCHES || length <= 0 || length >= COMP_NAME_MAX ||
      prefix_len > length || !starts_with_ci(name, prefix, prefix_len) ||
      comp_contains(name, length))
    return;
  for (int i = 0; i < length; i++)
    comp_matches[comp_count][i] = name[i];
  comp_matches[comp_count][length] = 0;
  comp_count++;
}

static void comp_scan_dir(const char *dir, const char *prefix, int prefix_len, int commands_only) {
  char dbuf[512];
  unsigned idx = 0;
  while (comp_count < COMP_MAX_MATCHES) {
    long n = readdir_path(dir, &idx, dbuf, sizeof(dbuf));
    if (n <= 0)
      break;
    long off = 0;
    while (off < n && comp_count < COMP_MAX_MATCHES) {
      const char *name = dbuf + off;
      int nl           = (int)strlen(name);
      int display_len  = nl;
      if (commands_only) {
        if (ends_with_ci(name, nl, ".elf") || ends_with_ci(name, nl, ".exe"))
          display_len -= 4;
        else
          display_len = 0;
      }
      comp_add(name, display_len, prefix, prefix_len);
      off += (long)nl + 1;
    }
  }
}

/* Command words come from PATH and have their executable suffix hidden.
 * Other words retain ordinary current-directory filename completion. */
static void comp_scan(const char *prefix, int prefix_len, int command_word) {
  comp_count = 0;
  comp_index = 0;

  if (!command_word) {
    comp_scan_dir(".", prefix, prefix_len, 0);
    return;
  }

  int cursor = 0;
  char dir[EXEC_DIR_MAX];
  int status;
  while ((status = path_next(shell_exec_path(), &cursor, dir, sizeof(dir))) != 0) {
    if (status > 0)
      comp_scan_dir(dir, prefix, prefix_len, 1);
  }
}

/* Erase the previously-completed word from buf and the screen, then write
 * comp_matches[comp_index] in its place. */
static void comp_apply(char *buf, int *n_ptr, int max) {
  int n      = *n_ptr;
  int wstart = n - comp_word_len;
  if (wstart < 0)
    wstart = 0;

  for (int i = 0; i < comp_word_len; i++)
    console_puts("\b \b");

  const char *m = comp_matches[comp_index];
  int ml        = (int)strlen(m);
  if (wstart + ml >= max)
    ml = max - wstart - 1;
  if (ml < 0)
    ml = 0;

  for (int i = 0; i < ml; i++) {
    buf[wstart + i] = m[i];
    console_putc(m[i]);
  }
  *n_ptr        = wstart + ml;
  comp_word_len = ml;
}

/* TAB handler. First press snapshots the word and scans the FS; further
 * consecutive presses (no other input in between) cycle the match list. */
void handle_tab(char *buf, int *n_ptr, int max, int continuing) {
  int n = *n_ptr;
  if (!continuing) {
    int ws = word_start(buf, n);
    int wl = n - ws;
    char prefix[COMP_NAME_MAX];
    if (wl >= COMP_NAME_MAX)
      wl = COMP_NAME_MAX - 1;
    for (int i = 0; i < wl; i++)
      prefix[i] = buf[ws + i];
    prefix[wl] = 0;

    comp_word_len = wl;
    comp_scan(prefix, wl, is_command_word(buf, ws));
    if (comp_count == 0)
      return; /* no matches; leave buf as-is */
    comp_index = 0;
    comp_apply(buf, n_ptr, max);
  } else {
    if (comp_count == 0)
      return;
    comp_index = (comp_index + 1) % comp_count;
    comp_apply(buf, n_ptr, max);
  }
}
