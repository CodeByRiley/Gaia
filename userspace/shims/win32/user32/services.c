#include <lib/services.h>
#include <lib/syscall.h>
long ipc_send(int pid, const struct ipc_msg *m) {
  return gaia_call(SYS_IPC_SEND, pid, (int64_t)(uintptr_t)m, 0, 0, 0, 0);
}
long ipc_recv(struct ipc_msg *m) {
  return gaia_call(SYS_IPC_RECV, (int64_t)(uintptr_t)m, 0, 0, 0, 0, 0);
}
long wm_pid(void) { return gaia_call(SYS_WM_PID, 0, 0, 0, 0, 0, 0); }
long get_ticks(void) { return gaia_call(SYS_GET_TICKS, 0, 0, 0, 0, 0, 0); }
long sleep_ticks(unsigned long n) { return gaia_call(SYS_SLEEP_TICKS, n, 0, 0, 0, 0, 0); }
