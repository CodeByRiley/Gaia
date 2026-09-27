/* userspace/bin/pipetest/pipetest.c - pipes, dup and wait4, via musl.
 *
 * Like udpecho, everything here is plain POSIX except spawn(): pipe, pipe2,
 * dup, dup2, poll, fstat, lseek and waitpid are musl's own wrappers on the
 * Linux syscall numbers. Gaia has no fork, so a pipeline is built the way
 * a spawn-only shell builds it: point our own stdout at the pipe, spawn a
 * child (which inherits fds 0-2), put stdout back.
 *
 * Each check prints "pipetest: check NAME ok" or "... FAILED detail";
 * tests/pipe_test.py fails on any FAILED line or a missing "all ok".
 *
 * Child modes, run by the checks themselves:
 *   pipetest writer N   write N pattern bytes to stdout, exit 7
 *   pipetest sleeper    sleep 300 ms, exit 3
 */
#include <errno.h>
#include <lib/syscall.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define SELF "/usr/bin/pipetest.elf"
#define BIG_WRITE 20000

static int failures;

static void check(const char *name, int ok, const char *detail) {
    if (ok) {
        printf("pipetest: check %s ok\n", name);
    } else {
        printf("pipetest: check %s FAILED %s errno=%d\n", name, detail, errno);
        failures++;
    }
}

static unsigned char pattern(long i) { return (unsigned char)('a' + i % 26); }

static int run_writer(long n) {
    unsigned char buf[1000];
    long done = 0;
    while (done < n) {
        long chunk = n - done < (long)sizeof(buf) ? n - done : (long)sizeof(buf);
        for (long i = 0; i < chunk; i++)
            buf[i] = pattern(done + i);
        long w = write(1, buf, (size_t)chunk);
        if (w <= 0)
            return 1;
        done += w;
    }
    return 7;
}

static void check_basic(void) {
    int fds[2];
    int rc = pipe(fds);
    check("pipe", rc == 0 && fds[0] >= 3 && fds[1] > fds[0], "pipe()");
    if (rc != 0)
        return;

    char buf[16] = {0};
    long w = write(fds[1], "hello", 5);
    long r = read(fds[0], buf, sizeof(buf));
    check("roundtrip", w == 5 && r == 5 && memcmp(buf, "hello", 5) == 0,
          "write/read");

    struct stat st;
    check("fstat-fifo", fstat(fds[0], &st) == 0 && S_ISFIFO(st.st_mode),
          "fstat");
    errno = 0;
    check("lseek-espipe", lseek(fds[0], 0, SEEK_SET) < 0 && errno == ESPIPE,
          "lseek");

    close(fds[1]);
    r = read(fds[0], buf, sizeof(buf));
    check("eof", r == 0, "read after writer closed");
    close(fds[0]);
}

static void check_epipe(void) {
    int fds[2];
    if (pipe(fds) != 0) {
        check("epipe", 0, "pipe()");
        return;
    }
    close(fds[0]);
    errno = 0;
    long w = write(fds[1], "x", 1);
    check("epipe", w < 0 && errno == EPIPE, "write with no reader");
    close(fds[1]);
}

static void check_dup(void) {
    int fds[2];
    if (pipe(fds) != 0) {
        check("dup", 0, "pipe()");
        return;
    }
    int d = dup(fds[1]);
    close(fds[1]);
    char c = 0;
    long w = write(d, "z", 1);
    long r = read(fds[0], &c, 1);
    check("dup", d >= 3 && w == 1 && r == 1 && c == 'z', "write through dup");

    /* The dup is the last write end: closing it must give EOF. */
    close(d);
    r = read(fds[0], &c, 1);
    check("dup-eof", r == 0, "EOF after last dup closed");
    close(fds[0]);
}

static void check_poll(void) {
    int fds[2];
    if (pipe(fds) != 0) {
        check("poll", 0, "pipe()");
        return;
    }
    struct pollfd p = {.fd = fds[0], .events = POLLIN};
    int n = poll(&p, 1, 50);
    check("poll-empty", n == 0, "poll on empty pipe");

    write(fds[1], "!", 1);
    p.revents = 0;
    n = poll(&p, 1, 1000);
    check("poll-data", n == 1 && (p.revents & POLLIN), "poll after write");

    char c;
    read(fds[0], &c, 1);
    close(fds[1]);
    p.revents = 0;
    n = poll(&p, 1, 1000);
    check("poll-hup", n == 1 && (p.revents & POLLHUP), "poll after close");
    close(fds[0]);

    if (pipe2(fds, O_NONBLOCK) != 0) {
        check("nonblock", 0, "pipe2()");
        return;
    }
    errno = 0;
    long r = read(fds[0], &c, 1);
    check("nonblock", r < 0 && errno == EAGAIN, "read on empty O_NONBLOCK");
    close(fds[0]);
    close(fds[1]);
}

/* The pipeline: our stdout becomes the pipe for exactly as long as it takes
 * to spawn the child. BIG_WRITE is several times the pipe's capacity, so the
 * child blocks on a full pipe until we drain it. */
static void check_child_pipeline(void) {
    int fds[2];
    if (pipe(fds) != 0) {
        check("pipeline", 0, "pipe()");
        return;
    }

    fflush(stdout);
    int saved = dup(1);
    dup2(fds[1], 1);
    close(fds[1]);

    char count[16];
    snprintf(count, sizeof(count), "%d", BIG_WRITE);
    char *argv[] = {"pipetest", "writer", count, NULL};
    long pid = spawn(SELF, argv);

    dup2(saved, 1);
    close(saved);

    if (pid <= 0) {
        check("pipeline", 0, "spawn");
        close(fds[0]);
        return;
    }

    long total = 0;
    int intact = 1;
    unsigned char buf[512];
    for (;;) {
        long r = read(fds[0], buf, sizeof(buf));
        if (r <= 0)
            break;
        for (long i = 0; i < r; i++)
            if (buf[i] != pattern(total + i))
                intact = 0;
        total += r;
    }
    close(fds[0]);
    check("pipeline", total == BIG_WRITE && intact, "bytes through child");

    int status = 0;
    long got = waitpid((pid_t)pid, &status, 0);
    check("wait-status",
          got == pid && WIFEXITED(status) && WEXITSTATUS(status) == 7,
          "waitpid on pipeline child");
}

static void check_wait(void) {
    /* A child that exits before we wait: collected from its exit record,
     * after the reaper has had time to free its slot. */
    char *quick[] = {"pipetest", "writer", "0", NULL};
    long pid = spawn(SELF, quick);
    usleep(300000);
    int status = 0;
    long got = waitpid((pid_t)pid, &status, 0);
    check("wait-late",
          pid > 0 && got == pid && WIFEXITED(status) && WEXITSTATUS(status) == 7,
          "waitpid after child already exited");

    char *slow[] = {"pipetest", "sleeper", NULL};
    pid = spawn(SELF, slow);
    got = waitpid((pid_t)pid, &status, WNOHANG);
    check("wait-nohang", pid > 0 && got == 0, "WNOHANG on running child");
    got = waitpid(-1, &status, 0);
    check("wait-any",
          got == pid && WIFEXITED(status) && WEXITSTATUS(status) == 3,
          "waitpid(-1) blocks for the running child");

    errno = 0;
    got = waitpid(-1, &status, 0);
    check("wait-echild", got < 0 && errno == ECHILD, "no children left");
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "writer") == 0)
        return run_writer(atol(argv[2]));
    if (argc == 2 && strcmp(argv[1], "sleeper") == 0) {
        usleep(300000);
        return 3;
    }

    check_basic();
    check_epipe();
    check_dup();
    check_poll();
    check_child_pipeline();
    check_wait();

    if (failures == 0)
        printf("pipetest: all ok\n");
    else
        printf("pipetest: %d FAILED\n", failures);
    return failures ? 1 : 0;
}
