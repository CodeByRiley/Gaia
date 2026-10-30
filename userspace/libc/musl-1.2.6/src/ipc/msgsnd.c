#include "ipc.h"
#include "syscall.h"
#include <sys/msg.h>

int msgsnd(int q, const void *m, size_t len, int flag) {
#ifndef SYS_ipc
  return syscall_cp(SYS_msgsnd, q, m, len, flag);
#else
  return syscall_cp(SYS_ipc, IPCOP_msgsnd, q, len, flag, m);
#endif
}
