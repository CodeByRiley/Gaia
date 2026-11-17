#include <stdio.h>
#include <netdb.h>
#include <arpa/inet.h>

int main(void)
{
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM
    };

    struct addrinfo *result;

    int rc = getaddrinfo("www.google.com", NULL,
                         &hints, &result);

    if (rc != 0) {
        printf("DNS failed: %s\n", gai_strerror(rc));
        return 1;
    }

    for (struct addrinfo *p = result; p; p = p->ai_next) {
        struct sockaddr_in *addr =
            (struct sockaddr_in *)p->ai_addr;

        char ip[INET_ADDRSTRLEN];

        inet_ntop(AF_INET, &addr->sin_addr,
                  ip, sizeof(ip));

        printf("Resolved: %s\n", ip);
    }

    freeaddrinfo(result);
    return 0;
}
