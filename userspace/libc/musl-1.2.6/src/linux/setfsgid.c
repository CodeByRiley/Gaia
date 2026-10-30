#include "syscall.h"
#include <sys/fsuid.h>

int setfsgid(gid_t gid) { return syscall(SYS_setfsgid, gid); }
