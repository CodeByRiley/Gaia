#ifndef NET_DHCP_H
#define NET_DHCP_H

#include <net/inet.h>
#include <utilities/types.h>

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_MAGIC_COOKIE 0x63825363u

enum dhcp_state {
    DHCP_INIT,
    DHCP_SELECTING,
    DHCP_REQUESTING,
    DHCP_BOUND,
    DHCP_RENEWING,
    DHCP_REBINDING
};

enum dhcp_message {
    DHCP_DISCOVER = 1,
    DHCP_OFFER    = 2,
    DHCP_REQUEST  = 3,
    DHCP_DECLINE  = 4,
    DHCP_ACK      = 5,
    DHCP_NAK      = 6,
    DHCP_RELEASE  = 7,
    DHCP_INFORM   = 8
};

enum dhcp_option {
    DHCP_OPT_PAD       = 0,
    DHCP_OPT_SUBNET    = 1,
    DHCP_OPT_ROUTER    = 3,
    DHCP_OPT_DNS       = 6,
    DHCP_OPT_REQUESTED = 50,
    DHCP_OPT_LEASE     = 51,
    DHCP_OPT_TYPE      = 53,
    DHCP_OPT_SERVER    = 54,
    DHCP_OPT_PARAMS    = 55,
    DHCP_OPT_T1        = 58,
    DHCP_OPT_T2        = 59,
    DHCP_OPT_END       = 255
};

struct dhcp_header {
    u8 op;
    u8 htype;
    u8 hlen;
    u8 hops;

    u32 xid;
    u16 secs;
    u16 flags;

    u8 ciaddr[4];
    u8 yiaddr[4];
    u8 siaddr[4];
    u8 giaddr[4];

    u8 chaddr[16];
    u8 sname[64];
    u8 file[128];

    u32 cookie;
} PACKED;

struct dhcp_lease {
    u8 address[4];
    u8 netmask[4];
    u8 gateway[4];
    u8 server[4];

    u8 dns[4][4];
    u8 dns_count;

    u32 duration;
    u32 renewal;
    u32 rebinding;
};

void dhcp_start(void);
void dhcp_stop(void);
void dhcp_tick(void);

void dhcp_input(const u8 *payload, u16 len);

enum dhcp_state dhcp_get_state(void);
const struct dhcp_lease *dhcp_get_lease(void);



#endif
