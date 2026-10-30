#ifndef SHELL_HISTORY_H
#define SHELL_HISTORY_H

#define CMD_HISTORY 128

void history_add(const char *command);
int history_count(void);
const char *history_get(int index);

#endif
