#include "syscall.h"
#include <sys/fsuid.h>

int setfsuid(uid_t uid) { return syscall(SYS_setfsuid, uid); }
