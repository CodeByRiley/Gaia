#ifndef SKALD_COMPLETE_H
#define SKALD_COMPLETE_H

#define COMP_MAX_MATCHES 32
#define COMP_NAME_MAX 64

void handle_tab(char *buf, int *length, int max,
                int continuing);

#endif
