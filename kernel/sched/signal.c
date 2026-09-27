/* kernel/sched/signal.c , see signal.h. */
#include <sched/signal.h>
#include <arch/irq.h>
#include <arch/syscall.h>
#include <interrupts/idt.h>
#include <loader/process.h>
#include <memory/uvm.h>
#include <sched/sched.h>
#include <utilities/errno.h>
#include <utilities/log.h>
#include <utilities/string.h>

/* Linux x86_64 struct sigcontext. It doubles as the one register set both
 * return paths convert to and from, so the frame builder is written once. */
struct linux_sigcontext {
  u64 r8, r9, r10, r11, r12, r13, r14, r15;
  u64 rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp;
  u64 rip, eflags;
  u16 cs, gs, fs, ss;
  u64 err, trapno, oldmask, cr2;
  u64 fpstate; /* user address of the fxsave image */
  u64 reserved[8];
};
_Static_assert(sizeof(struct linux_sigcontext) == 256,
               "sigcontext must match Linux x86_64");

struct linux_ucontext {
  u64 uc_flags;
  u64 uc_link;
  u64 ss_sp;
  u32 ss_flags;
  u32 ss_pad;
  u64 ss_size;
  struct linux_sigcontext mcontext;
  u64 sigmask;
};
_Static_assert(offsetof(struct linux_ucontext, mcontext) == 40,
               "ucontext.uc_mcontext offset is Linux ABI");

struct linux_siginfo {
  int si_signo;
  int si_errno;
  int si_code;
  int pad;
  int si_pid;
  int si_uid;
  u8 rest[104];
};
_Static_assert(sizeof(struct linux_siginfo) == 128, "siginfo is 128 bytes");

/* What sits at the handler's rsp. pretcode is its return address, so the
 * handler's `ret` lands in sa_restorer with rsp pointing at uc , which is
 * how signal_sigreturn finds the frame again. */
struct rt_sigframe {
  u64 pretcode;
  struct linux_ucontext uc;
  struct linux_siginfo info;
};

#define FXSAVE_BYTES 512
#define SI_USER 0
#define RED_ZONE 128
#define RFLAGS_TF (1ULL << 8)
#define RFLAGS_DF (1ULL << 10)
#define MXCSR_OFFSET 24
/* Reserved MXCSR bits make fxrstor fault; a handler can scribble on them. */
#define MXCSR_VALID 0x0000FFFFU

/* ---- dispositions ------------------------------------------------------ */

/* Default action is to do nothing. No job control, so the stop signals are
 * in this set too rather than stopping anything. */
static int default_ignored(int sig) {
  switch (sig) {
  case LINUX_SIGCHLD:
  case LINUX_SIGCONT:
  case LINUX_SIGURG:
  case LINUX_SIGWINCH:
  case LINUX_SIGSTOP:
  case LINUX_SIGTSTP:
  case LINUX_SIGTTIN:
  case LINUX_SIGTTOU:
    return 1;
  default:
    return 0;
  }
}

static const struct task_signal_action *action_of(struct task *t, int sig) {
  return &t->context->signal_actions[sig - 1];
}

/* Would delivery do nothing at all? Such a signal is dropped at send time,
 * as Linux does, rather than left pending. */
static int discarded(struct task *t, int sig) {
  if (sig == LINUX_SIGKILL)
    return 0;
  u64 handler = action_of(t, sig)->handler;
  return handler == LINUX_SIG_IGN ||
         (handler == LINUX_SIG_DFL && default_ignored(sig));
}

/* ---- sending ----------------------------------------------------------- */

static void make_pending(struct task *t, int sig) {
  u64 flags = irq_save();
  t->sig_pending |= SIG_BIT(sig);
  /* A task parked on a wait queue comes back with -EINTR so the signal is
   * seen now rather than whenever its data arrives. */
  if (task_signal_deliverable(t))
    task_interrupt(t);
  irq_restore(flags);
}

long signal_send(int pid, int sig) {
  if (sig < 0 || sig > TASK_SIGNAL_COUNT)
    return -EINVAL;
  struct task *t = task_find(pid);
  if (!t || !t->vm || !t->context || t->is_thread ||
      t->state == TASK_ZOMBIE || t->state == TASK_DEAD)
    return -ESRCH;
  if (sig == 0)
    return 0;

  /* Not loaded yet: nothing can have installed a handler, so every signal
   * that is not discarded is a default-action kill. */
  if (t->state == TASK_LOADING)
    return discarded(t, sig) ? 0 : (task_kill(pid, -sig) == 0 ? 0 : -EAGAIN);

  if (discarded(t, sig))
    return 0;

  /* An unblocked signal whose action is to terminate terminates now, the
   * same path kill always took , the target may be parked somewhere that
   * never returns to ring 3. Everything else waits for delivery. */
  u64 handler = action_of(t, sig)->handler;
  int blocked = (t->sig_blocked & SIG_BIT(sig)) != 0;
  if (sig == LINUX_SIGKILL || (handler == LINUX_SIG_DFL && !blocked)) {
    if (t == task_current())
      task_exit(-sig);
    /* task_kill refuses a task mid-VFS-operation; the caller may retry. */
    return task_kill(pid, -sig) == 0 ? 0 : -EAGAIN;
  }

  make_pending(t, sig);
  return 0;
}

void signal_child_exited(struct task *parent) {
  if (!parent || !parent->context || parent->state == TASK_ZOMBIE ||
      parent->state == TASK_DEAD)
    return;
  /* Only a handler makes SIGCHLD observable; default and ignore are both
   * "nothing happens", and wait4 does not need the signal. */
  if (action_of(parent, LINUX_SIGCHLD)->handler > LINUX_SIG_IGN)
    make_pending(parent, LINUX_SIGCHLD);
}

/* ---- delivery ---------------------------------------------------------- */

static void terminate(int sig) {
  log_write_hex("signal: terminating on signal", (u64)sig, KERNEL, LOG_INFO);
  task_exit(-sig);
}

static int user_code_address(u64 va) {
  return va >= USER_VA_MIN && va < USER_VA_MAX;
}

/* The common half of delivery. `sc` holds the registers about to return to
 * ring 3; on a handler it is rewritten to enter the handler instead.
 * Returns non-zero when it rewrote anything. */
static int deliver(struct linux_sigcontext *sc) {
  struct task *t = task_current();
  if (!t || !t->context || !t->vm)
    return 0;

  u64 flags = irq_save();
  u64 ready = t->sig_pending & ~t->sig_blocked;
  if (!ready) {
    irq_restore(flags);
    return 0;
  }
  int sig = __builtin_ctzll(ready) + 1;
  t->sig_pending &= ~SIG_BIT(sig);
  irq_restore(flags);

  struct task_signal_action *act = &t->context->signal_actions[sig - 1];
  if (act->handler == LINUX_SIG_IGN)
    return 0;
  if (act->handler == LINUX_SIG_DFL) {
    if (default_ignored(sig))
      return 0;
    terminate(sig);
  }

  /* x86_64 has no kernel-provided trampoline: without sa_restorer the
   * handler has nowhere to return to. musl always sets it. */
  if (!(act->flags & LINUX_SA_RESTORER) || !user_code_address(act->restorer) ||
      !user_code_address(act->handler))
    terminate(LINUX_SIGSEGV);

  /* Below the interrupted code's red zone: the FPU image, 64-aligned for
   * fxsave's 16, then the frame, placed so the handler starts with
   * rsp % 16 == 8 exactly as if it had been called. */
  u64 sp = sc->rsp - RED_ZONE;
  u64 fx = (sp - FXSAVE_BYTES) & ~63ULL;
  u64 frame_va = ((fx - sizeof(struct rt_sigframe)) & ~15ULL) - 8;
  if (frame_va < USER_VA_MIN ||
      !uvm_buffer_ok(t->vm, (void *)frame_va, sc->rsp - frame_va, 1))
    terminate(LINUX_SIGSEGV); /* no room on the stack: what Linux does too */

  struct rt_sigframe *frame = (struct rt_sigframe *)frame_va;
  memset(frame, 0, sizeof(*frame));
  frame->pretcode = act->restorer;
  frame->uc.mcontext = *sc;
  frame->uc.mcontext.fpstate = fx;
  frame->uc.mcontext.oldmask = t->sig_blocked;
  frame->uc.sigmask = t->sig_blocked;
  frame->info.si_signo = sig;
  frame->info.si_code = SI_USER;
  /* The state saved on entry from ring 3, not the live registers: kernel
   * code has been running on those since. */
  memcpy((void *)fx, t->context->user_fx, FXSAVE_BYTES);

  u64 mask = act->mask;
  if (!(act->flags & LINUX_SA_NODEFER))
    mask |= SIG_BIT(sig);
  t->sig_blocked = (t->sig_blocked | mask) & ~SIG_UNBLOCKABLE;
  u64 handler = act->handler;
  if (act->flags & LINUX_SA_RESETHAND)
    act->handler = LINUX_SIG_DFL;

  sc->rip = handler;
  sc->rsp = frame_va;
  sc->rdi = (u64)sig;
  sc->rsi = (u64)&frame->info;
  sc->rdx = (u64)&frame->uc;
  sc->rax = 0;
  sc->eflags &= ~(RFLAGS_DF | RFLAGS_TF);
  return 1;
}

/* The two return paths save the same general registers in different
 * orders. These move them through linux_sigcontext and back. */
#define REGS_TO_SC(sc, f)                                                      \
  do {                                                                         \
    (sc).r8 = (f)->r8; (sc).r9 = (f)->r9; (sc).r10 = (f)->r10;                 \
    (sc).r11 = (f)->r11; (sc).r12 = (f)->r12; (sc).r13 = (f)->r13;             \
    (sc).r14 = (f)->r14; (sc).r15 = (f)->r15; (sc).rdi = (f)->rdi;             \
    (sc).rsi = (f)->rsi; (sc).rbp = (f)->rbp; (sc).rbx = (f)->rbx;             \
    (sc).rdx = (f)->rdx; (sc).rax = (f)->rax; (sc).rcx = (f)->rcx;             \
    (sc).rsp = (f)->rsp; (sc).rip = (f)->rip; (sc).eflags = (f)->rflags;       \
    (sc).cs = (u16)(f)->cs; (sc).ss = (u16)(f)->ss;                            \
  } while (0)

#define SC_TO_REGS(f, sc)                                                      \
  do {                                                                         \
    (f)->r8 = (sc).r8; (f)->r9 = (sc).r9; (f)->r10 = (sc).r10;                 \
    (f)->r11 = (sc).r11; (f)->r12 = (sc).r12; (f)->r13 = (sc).r13;             \
    (f)->r14 = (sc).r14; (f)->r15 = (sc).r15; (f)->rdi = (sc).rdi;             \
    (f)->rsi = (sc).rsi; (f)->rbp = (sc).rbp; (f)->rbx = (sc).rbx;             \
    (f)->rdx = (sc).rdx; (f)->rax = (sc).rax; (f)->rcx = (sc).rcx;             \
    (f)->rsp = (sc).rsp; (f)->rip = (sc).rip; (f)->rflags = (sc).eflags;       \
  } while (0)

void signal_deliver_syscall(struct syscall_frame *f) {
  struct linux_sigcontext sc;
  memset(&sc, 0, sizeof(sc));
  REGS_TO_SC(sc, f);
  if (deliver(&sc))
    SC_TO_REGS(f, sc);
}

void signal_deliver_irq(struct interrupt_frame *r) {
  if ((r->cs & 3) != 3)
    return;
  struct linux_sigcontext sc;
  memset(&sc, 0, sizeof(sc));
  REGS_TO_SC(sc, r);
  sc.err = r->err_code;
  sc.trapno = r->int_num;
  if (deliver(&sc))
    SC_TO_REGS(r, sc);
}

/* ---- return ------------------------------------------------------------ */

long signal_sigreturn(struct syscall_frame *f) {
  struct task *t = task_current();
  /* The handler's ret consumed pretcode, so rsp is the ucontext. */
  u64 uc_va = f->rsp;
  if (!t || !t->vm ||
      !uvm_buffer_ok(t->vm, (void *)uc_va, sizeof(struct linux_ucontext), 0))
    terminate(LINUX_SIGSEGV);

  struct linux_ucontext uc;
  memcpy(&uc, (void *)uc_va, sizeof(uc));
  struct linux_sigcontext *sc = &uc.mcontext;

  /* CS and SS are never taken from user memory; syscall_prepare_return
   * validates RIP and RSP and sanitises RFLAGS before the iretq. */
  SC_TO_REGS(f, *sc);

  if (sc->fpstate) {
    if ((sc->fpstate & 15) ||
        !uvm_buffer_ok(t->vm, (void *)sc->fpstate, FXSAVE_BYTES, 0))
      terminate(LINUX_SIGSEGV);
    /* Into the image the syscall exit restores. */
    u8 *fx = t->context->user_fx;
    memcpy(fx, (void *)sc->fpstate, FXSAVE_BYTES);
    u32 mxcsr;
    memcpy(&mxcsr, fx + MXCSR_OFFSET, sizeof(mxcsr));
    mxcsr &= MXCSR_VALID;
    memcpy(fx + MXCSR_OFFSET, &mxcsr, sizeof(mxcsr));
  }

  u64 flags = irq_save();
  t->sig_blocked = uc.sigmask & ~SIG_UNBLOCKABLE;
  irq_restore(flags);
  return (long)f->rax;
}

/* ---- masks ------------------------------------------------------------- */

#define LINUX_SIG_BLOCK 0
#define LINUX_SIG_UNBLOCK 1
#define LINUX_SIG_SETMASK 2

long signal_sigprocmask(int how, const u64 *set, u64 *old, usize size) {
  struct task *t = task_current();
  if (!t || !t->vm || size != sizeof(u64))
    return -EINVAL;
  if (set && !uvm_buffer_ok(t->vm, set, sizeof(*set), 0))
    return -EFAULT;
  if (old && !uvm_buffer_ok(t->vm, old, sizeof(*old), 1))
    return -EFAULT;

  u64 previous = t->sig_blocked;
  if (set) {
    u64 next;
    switch (how) {
    case LINUX_SIG_BLOCK:
      next = previous | *set;
      break;
    case LINUX_SIG_UNBLOCK:
      next = previous & ~*set;
      break;
    case LINUX_SIG_SETMASK:
      next = *set;
      break;
    default:
      return -EINVAL;
    }
    /* Unblocking a pending signal delivers it on this syscall's return. */
    t->sig_blocked = next & ~SIG_UNBLOCKABLE;
  }
  if (old)
    *old = previous;
  return 0;
}

long signal_sigpending(u64 *out, usize size) {
  struct task *t = task_current();
  if (!t || !t->vm || size != sizeof(u64))
    return -EINVAL;
  if (!uvm_buffer_ok(t->vm, out, sizeof(*out), 1))
    return -EFAULT;
  *out = t->sig_pending & t->sig_blocked;
  return 0;
}
