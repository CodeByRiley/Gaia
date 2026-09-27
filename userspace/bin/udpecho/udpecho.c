/* userspace/bin/udpecho/udpecho.c - echo UDP datagrams, via musl sockets.
 *
 * The point of this program is what it does NOT contain: no <lib/syscall.h>,
 * no Gaia-specific call, nothing but <sys/socket.h> and <netinet/in.h>.
 * Every syscall it makes is issued by musl itself, on the Linux numbers -
 * socket is 41, bind 49, sendto 44, recvfrom 45. If those are not mirrored
 * in the kernel this program does not merely misbehave, it fails at the
 * first call with "unknown syscall", which is exactly how the gap showed up.
 *
 * That makes it the honest test for ported software. NetSurf's fetcher, and
 * anything else brought over unmodified, will reach the network through
 * these same entry points; a Gaia-specific socket API would have proved
 * nothing about them.
 *
 * recvfrom blocks until a datagram arrives, so the loop is a plain loop.
 * Before it, and before each receive, the program checks the other waiting
 * paths on the same socket: MSG_DONTWAIT on an empty queue, a poll that has
 * to time out, and a poll that a datagram has to wake. Each prints a
 * "udpecho: check" line; tests/net_udp_test.py fails on any FAILED one.
 */
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define ECHO_PORT 7777
#define ECHO_ROUNDS 4

int main(int argc, char **argv) {
    int rounds = ECHO_ROUNDS;
    if (argc == 2) {
        rounds = 0;
        for (const char *p = argv[1]; *p; p++) {
            if (*p < '0' || *p > '9') {
                printf("udpecho: bad round count '%s'\n", argv[1]);
                return 1;
            }
            rounds = rounds * 10 + (*p - '0');
        }
        if (rounds <= 0) {
            printf("udpecho: rounds must be at least 1\n");
            return 1;
        }
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        printf("udpecho: socket failed\n");
        return 1;
    }
    printf("udpecho: socket fd=%d\n", fd);

    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(ECHO_PORT);
    local.sin_addr.s_addr = 0; /* INADDR_ANY */

    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        printf("udpecho: bind failed\n");
        close(fd);
        return 1;
    }
    /* Before announcing the port: the test sends as soon as it reads the
     * "listening" line, and a datagram landing here would fail both checks
     * for a reason that has nothing to do with the kernel. */
    {
        char probe[8];
        long n = recvfrom(fd, probe, sizeof(probe), MSG_DONTWAIT, NULL, NULL);
        if (n < 0 && errno == EAGAIN)
            printf("udpecho: check dontwait ok\n");
        else
            printf("udpecho: check dontwait FAILED n=%ld errno=%d\n", n, errno);

        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int r = poll(&pfd, 1, 100);
        if (r == 0 && pfd.revents == 0)
            printf("udpecho: check poll-timeout ok\n");
        else
            printf("udpecho: check poll-timeout FAILED r=%d revents=%d\n",
                   r, pfd.revents);
    }
    printf("udpecho: listening on %d\n", ECHO_PORT);

    for (int i = 0; i < rounds; i++) {
        char buf[512];
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);

        /* Block in poll rather than recvfrom, so the datagram has to wake
         * a task parked on the socket through poll's own entry. */
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int r = poll(&pfd, 1, -1);
        if (r == 1 && (pfd.revents & POLLIN))
            printf("udpecho: check poll-wake ok\n");
        else
            printf("udpecho: check poll-wake FAILED r=%d revents=%d\n",
                   r, pfd.revents);

        long n = recvfrom(fd, buf, sizeof(buf), 0,
                          (struct sockaddr *)&from, &fromlen);
        if (n < 0) {
            printf("udpecho: recvfrom failed\n");
            break;
        }

        printf("udpecho: got %ld bytes from port %d\n", n, ntohs(from.sin_port));

        /* Straight back to whoever sent it -- from is the proof that
         * recvfrom filled in a usable source address, not just a length. */
        long sent = sendto(fd, buf, (size_t)n, 0,
                           (struct sockaddr *)&from, fromlen);
        if (sent != n)
            printf("udpecho: sendto returned %ld for %ld bytes\n", sent, n);
        else
            printf("udpecho: echoed %ld bytes\n", sent);
    }

    close(fd);
    printf("udpecho: done\n");
    return 0;
}
