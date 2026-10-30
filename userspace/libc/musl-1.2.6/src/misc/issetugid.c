#define _BSD_SOURCE
#include "libc.h"
#include <unistd.h>

int issetugid(void) { return libc.secure; }
