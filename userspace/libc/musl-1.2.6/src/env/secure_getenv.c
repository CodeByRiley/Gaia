#define _GNU_SOURCE
#include "libc.h"
#include <stdlib.h>

char *secure_getenv(const char *name) { return libc.secure ? NULL : getenv(name); }
