#include "dynlink.h"
#include <dlfcn.h>

void *dlsym(void *restrict p, const char *restrict s) { return __dlsym(p, s, 0); }
