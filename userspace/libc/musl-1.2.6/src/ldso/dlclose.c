#include "dynlink.h"
#include <dlfcn.h>

int dlclose(void *p) { return __dl_invalid_handle(p); }
