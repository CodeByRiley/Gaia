#include "ipc.h"
#include "syscall.h"
#include <sys/msg.h>

int msgget(key_t k, int flag) {
#ifndef SYS_ipc
  return syscall(SYS_msgget, k, flag);
#else
  return syscall(SYS_ipc, IPCOP_msgget, k, flag);
#endif
}
