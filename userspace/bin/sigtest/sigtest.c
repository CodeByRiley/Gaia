/* userspace/bin/sigtest/sigtest.c - signal delivery, via musl.
 *
 * Plain POSIX except spawn(): sigaction, raise, kill, sigprocmask,
 * sigpending and waitpid are musl's own, issuing rt_sigaction, tkill, kill,
 * rt_sigprocmask and wait4 and returning through __restore_rt and
 * rt_sigreturn exactly as they would on Linux.
 *
 * Each check prints "sigtest: check NAME ok" or "... FAILED detail";
 * tests/signal_test.py fails on any FAILED line or a missing "all ok".
 *
 * Child modes, run by the checks themselves:
 *   sigtest killer PID MS   sleep MS, then SIGUSR1 to PID, exit 0
 *   sigtest spinner         catch SIGUSR1, spin in pure user code until it
 *                           arrives, exit 5
 *   sigtest spin            spin with no handler at all
 */
#include <errno.h>
#include <lib/syscall.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define SELF "/usr/bin/sigtest.elf"

static int failures;

static void check(const char *name, int ok, const char *detail) {
    if (ok) {
        printf("sigtest: check %s ok\n", name);
    } else {
        printf("sigtest: check %s FAILED %s errno=%d\n", name, detail, errno);
        failures++;
    }
}

static volatile sig_atomic_t hits;
static volatile int last_signo;
static volatile int last_info_signo;
static volatile double handler_math;

static void on_signal(int sig, siginfo_t *info, void *uc) {
    (void)uc;
    hits++;
    last_signo = sig;
    last_info_signo = info ? info->si_signo : -1;
    /* Dirty the SSE registers the interrupted code is using. */
    volatile double a = 3.25, b = 7.5;
    handler_math = a * b + (double)sig;
}

static void install(int sig, int flags) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | flags;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

/* Not a signal check, but the one the signal frame depends on: the syscall
 * ABI preserves every register except rax, rcx and r11, SSE included, and
 * musl's wrappers declare no xmm clobbers. */
static void check_fpu_across_syscall(void) {
    unsigned long out;
    __asm__ volatile(
        "mov $0x5151515152525252, %%rax\n\t"
        "movq %%rax, %%xmm0\n\t"
        "mov $39, %%eax\n\t" /* SYS_getpid */
        "syscall\n\t"
        "movq %%xmm0, %[out]\n\t"
        : [out] "=m"(out)
        :
        : "rax", "rcx", "r11", "xmm0", "memory");
    check("fpu-syscall", out == 0x5151515152525252UL,
          "xmm0 changed across a syscall");

    /* The kernel's rt_sigaction zeroes a struct through xmm0 before it does
     * anything else (pxor xmm0 in its prologue at the time of writing), so
     * this one fails whenever user FPU state is not saved on entry. */
    static unsigned char oldact[32];
    __asm__ volatile(
        "mov $0x6161616162626262, %%rax\n\t"
        "movq %%rax, %%xmm0\n\t"
        "mov $12, %%edi\n\t"   /* SIGUSR2 */
        "xor %%esi, %%esi\n\t" /* query only */
        "lea %[old], %%rdx\n\t"
        "mov $8, %%r10d\n\t"   /* sigset size */
        "mov $13, %%eax\n\t"   /* SYS_rt_sigaction */
        "syscall\n\t"
        "movq %%xmm0, %[out]\n\t"
        : [out] "=m"(out), [old] "+m"(oldact)
        :
        : "rax", "rcx", "rdx", "rdi", "rsi", "r10", "r11", "xmm0", "memory");
    check("fpu-syscall-busy", out == 0x6161616162626262UL,
          "xmm0 changed across rt_sigaction");
}

static void check_handler(void) {
    install(SIGUSR1, 0);
    hits = 0;

    /* Raw tkill with sentinels in registers the handler entry is guaranteed
     * to overwrite: the kernel puts &uc in rdx, and the handler's own math
     * runs through xmm0. Only a working rt_sigreturn gives them back. C
     * cannot pin values to registers across a call, hence the asm. */
    unsigned long out_rdx, out_r8, out_r9, out_xmm0, out_rax;
    unsigned long tid = (unsigned long)getpid();
    __asm__ volatile(
        "mov $0xdddd0001, %%rdx\n\t"
        "mov $0x88880002, %%r8\n\t"
        "mov $0x99990003, %%r9\n\t"
        "mov $0x4242424243434343, %%rax\n\t"
        "movq %%rax, %%xmm0\n\t"
        "mov %[tid], %%rdi\n\t"
        "mov $10, %%esi\n\t"   /* SIGUSR1 */
        "mov $200, %%eax\n\t"  /* SYS_tkill */
        "syscall\n\t"
        "mov %%rax, %[rax]\n\t"
        "mov %%rdx, %[rdx]\n\t"
        "mov %%r8, %[r8]\n\t"
        "mov %%r9, %[r9]\n\t"
        "movq %%xmm0, %[x0]\n\t"
        : [rax] "=m"(out_rax), [rdx] "=m"(out_rdx), [r8] "=m"(out_r8),
          [r9] "=m"(out_r9), [x0] "=m"(out_xmm0)
        : [tid] "m"(tid)
        : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
          "xmm0", "memory");

    check("handler", hits == 1 && last_signo == SIGUSR1, "handler did not run");
    check("siginfo", last_info_signo == SIGUSR1, "si_signo");
    printf("sigtest: regs rax=%lx rdx=%lx r8=%lx r9=%lx xmm0=%lx\n", out_rax,
           out_rdx, out_r8, out_r9, out_xmm0);
    check("registers",
          out_rax == 0 && out_rdx == 0xdddd0001UL && out_r8 == 0x88880002UL &&
              out_r9 == 0x99990003UL && out_xmm0 == 0x4242424243434343UL,
          "caller-saved state after sigreturn");
}

static void check_mask(void) {
    install(SIGUSR1, 0);
    hits = 0;
    sigset_t block, pending;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    sigprocmask(SIG_BLOCK, &block, NULL);
    raise(SIGUSR1);
    check("mask-holds", hits == 0, "blocked signal ran");

    sigemptyset(&pending);
    sigpending(&pending);
    check("sigpending", sigismember(&pending, SIGUSR1) == 1, "not pending");

    sigprocmask(SIG_UNBLOCK, &block, NULL);
    check("mask-release", hits == 1, "not delivered on unblock");
}

static void check_ignore(void) {
    signal(SIGUSR2, SIG_IGN);
    raise(SIGUSR2);
    check("ignore", 1, "");
    signal(SIGUSR2, SIG_DFL);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    errno = 0;
    check("sigkill-uncatchable",
          sigaction(SIGKILL, &sa, NULL) < 0 && errno == EINVAL,
          "sigaction(SIGKILL) accepted");
}

/* A child kills us while we are parked in read() on an empty pipe. */
static void check_eintr(void) {
    install(SIGUSR1, 0);
    hits = 0;
    int fds[2];
    if (pipe(fds) != 0) {
        check("eintr", 0, "pipe");
        return;
    }
    char pid[16];
    snprintf(pid, sizeof(pid), "%d", (int)getpid());
    char *argv[] = {"sigtest", "killer", pid, "200", NULL};
    long child = spawn(SELF, argv);

    char c;
    errno = 0;
    long r = read(fds[0], &c, 1);
    check("eintr", child > 0 && r < 0 && errno == EINTR && hits == 1,
          "blocked read was not interrupted");
    close(fds[0]);
    close(fds[1]);
    int status;
    waitpid((pid_t)child, &status, 0);
}

/* The child never makes a syscall while it waits, so only the IRQ return
 * path can deliver to it. */
static void check_preempted(void) {
    char *argv[] = {"sigtest", "spinner", NULL};
    long child = spawn(SELF, argv);
    usleep(300000);
    kill((pid_t)child, SIGUSR1);
    int status = 0;
    long got = waitpid((pid_t)child, &status, 0);
    check("preempted", got == child && WIFEXITED(status) &&
                           WEXITSTATUS(status) == 5,
          "spinning child did not run its handler");
}

static void check_default_kill(void) {
    char *argv[] = {"sigtest", "spin", NULL};
    long child = spawn(SELF, argv);
    usleep(200000);
    kill((pid_t)child, SIGTERM);
    int status = 0;
    long got = waitpid((pid_t)child, &status, 0);
    check("default-kill", got == child && WIFSIGNALED(status) &&
                              WTERMSIG(status) == SIGTERM,
          "SIGTERM did not kill by signal");
}

static void check_sigchld(void) {
    install(SIGCHLD, 0);
    hits = 0;
    char *argv[] = {"sigtest", "killer", "0", "0", NULL};
    long child = spawn(SELF, argv);
    int status;
    long got;
    /* SIGCHLD may land during the wait and interrupt it. */
    while ((got = waitpid((pid_t)child, &status, 0)) < 0 && errno == EINTR)
        ;
    check("sigchld", got == child && hits >= 1 && last_signo == SIGCHLD,
          "no SIGCHLD");
    signal(SIGCHLD, SIG_DFL);
}

static volatile sig_atomic_t spin_flag;
static void on_spin(int sig) {
    (void)sig;
    spin_flag = 1;
}

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "killer") == 0) {
        usleep((useconds_t)atoi(argv[3]) * 1000);
        if (atoi(argv[2]) > 0)
            kill((pid_t)atoi(argv[2]), SIGUSR1);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "spinner") == 0) {
        signal(SIGUSR1, on_spin);
        while (!spin_flag)
            ;
        return 5;
    }
    if (argc == 2 && strcmp(argv[1], "spin") == 0) {
        for (;;)
            ;
    }

    check_fpu_across_syscall();
    check_handler();
    check_mask();
    check_ignore();
    check_eintr();
    check_preempted();
    check_default_kill();
    check_sigchld();

    if (failures == 0)
        printf("sigtest: all ok\n");
    else
        printf("sigtest: %d FAILED\n", failures);
    return failures ? 1 : 0;
}
