/* kernel/sched/signal.h , POSIX signal delivery.
 *
 * Model: Linux x86_64, as musl expects it, per task. A signal is sent to one
 * task by pid; Gaia threads are separate tasks with their own dispositions,
 * so there is no process-wide delivery to pick a thread for.
 *
 *   kill      signal_send: drop it (ignored), terminate the target (default
 *             action, or SIGKILL), or mark it pending and interrupt the
 *             target if it is parked on a wait queue.
 *   deliver   on every return to ring 3 , syscall exit and IRQ exit , the
 *             lowest pending unblocked signal runs: a handler gets a Linux
 *             rt_sigframe on the user stack and the return is redirected
 *             into it; a default action terminates the task.
 *   return    the handler returns into sa_restorer (musl's __restore_rt),
 *             which issues rt_sigreturn; signal_sigreturn restores the
 *             registers, FPU state and mask the frame recorded.
 *
 * Not supported: job control (SIGSTOP and the other stop signals are
 * ignored), sigaltstack, syscall restart (SA_RESTART syscalls return
 * EINTR), and siginfo beyond si_signo/si_code. Faults still terminate
 * directly rather than raising SIGSEGV to a handler.
 */
#ifndef SCHED_SIGNAL_H
#define SCHED_SIGNAL_H

#include <utilities/types.h>

struct task;
struct syscall_frame;
struct interrupt_frame;

#define LINUX_SIGINT 2
#define LINUX_SIGKILL 9
#define LINUX_SIGSEGV 11
#define LINUX_SIGTERM 15
#define LINUX_SIGCHLD 17
#define LINUX_SIGCONT 18
#define LINUX_SIGSTOP 19
#define LINUX_SIGTSTP 20
#define LINUX_SIGTTIN 21
#define LINUX_SIGTTOU 22
#define LINUX_SIGURG 23
#define LINUX_SIGWINCH 28

#define LINUX_SIG_DFL 0
#define LINUX_SIG_IGN 1

#define LINUX_SA_SIGINFO 0x00000004UL
#define LINUX_SA_RESTORER 0x04000000UL
#define LINUX_SA_NODEFER 0x40000000UL
#define LINUX_SA_RESETHAND 0x80000000UL

#define SIG_BIT(sig) (1ULL << ((sig) - 1))
/* Neither can be blocked, caught or ignored. */
#define SIG_UNBLOCKABLE (SIG_BIT(LINUX_SIGKILL) | SIG_BIT(LINUX_SIGSTOP))

/* Send `sig` to the task with this pid, as kill(2). sig 0 only checks that
 * the target exists. Returns 0 or a negative errno. */
long signal_send(int pid, int sig);

/* A child exited: SIGCHLD to its parent if the parent asked for it. */
void signal_child_exited(struct task *parent);

/* Run the next deliverable signal, if any, against the frame about to
 * return to ring 3. May not return: a default action exits the task. */
void signal_deliver_syscall(struct syscall_frame *f);
void signal_deliver_irq(struct interrupt_frame *r);

/* rt_sigreturn. Rewrites the whole frame from the signal frame on the user
 * stack and returns the restored rax, which the dispatcher must store
 * unchanged. */
long signal_sigreturn(struct syscall_frame *f);

/* rt_sigprocmask. */
long signal_sigprocmask(int how, const u64 *set, u64 *old, usize size);

/* rt_sigpending. */
long signal_sigpending(u64 *out, usize size);

#endif
