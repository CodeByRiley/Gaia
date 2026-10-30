#include "syscall.h"
#include <unistd.h>

int pause(void) { return sys_pause_cp(); }
