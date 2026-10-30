/* <gaia/ipc.h> , fixed-size messages and shared pages between processes.
 *
 * A struct ipc_msg is copied whole; the kernel fills in from_pid on the
 * receiving side, so a sender may leave it zero. Message types below
 * IPC_USER_FIRST are reserved for the system (IPC_PEER_EXITED, the window
 * manager protocol); a program's own protocol starts at IPC_USER_FIRST.
 *
 * Each process has one inbox. A program that also talks to the window
 * manager must take all of its messages through one receiver, because
 * ipc_recv() hands out whatever arrives next, whoever it was meant for.
 */
#ifndef GAIA_SDK_IPC_H
#define GAIA_SDK_IPC_H

#include <gaia/abi.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

long ipc_send(int target_pid, const struct ipc_msg *m);
/* Take the next message from this process's inbox. */
long ipc_recv(struct ipc_msg *out);

/* Map `npages` pages starting at the caller's page-aligned `in_va` into
 * `target_pid`.
 * The address they appear at in the target is written to *out_target_va;
 * pass it to the target in an ipc_msg. unshare removes the mapping again. */
long shmem_share(int target_pid, uint64_t in_va, long npages, uint64_t *out_target_va);
long shmem_unshare(int target_pid, uint64_t in_va, long npages);

#ifdef __cplusplus
}
#endif

#endif
