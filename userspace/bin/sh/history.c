#include "history.h"
#include "line.h"
#include <string.h>

static char history[CMD_HISTORY][LINE_MAX];

static int history_size = 0;
static int history_next = 0;

/* Add a command to history. */
void history_add(const char *command) {
  if (!command || !command[0])
    return;

  /* Avoid consecutive duplicates. */
  if (history_size > 0) {
    int previous = (history_next + CMD_HISTORY - 1) % CMD_HISTORY;

    if (strcmp(history[previous], command) == 0)
      return;
  }

  size_t length = strlen(command);

  if (length >= LINE_MAX)
    length = LINE_MAX - 1;

  memcpy(history[history_next], command, length);
  history[history_next][length] = '\0';

  history_next = (history_next + 1) % CMD_HISTORY;

  if (history_size < CMD_HISTORY)
    history_size++;
}

/* Number of commands currently stored. */
int history_count(void) { return history_size; }

/* Get a command by chronological index.
 *
 * 0 = oldest command
 * history_count() - 1 = newest command
 *
 * Returns NULL if the index is invalid.
 */
const char *history_get(int index) {
  if (index < 0 || index >= history_size)
    return NULL;

  int oldest = (history_next - history_size + CMD_HISTORY) % CMD_HISTORY;
  int slot   = (oldest + index) % CMD_HISTORY;

  return history[slot];
}
