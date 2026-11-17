#include <net/arp.h>
#include <net/dhcp.h>
#include <net/netif.h>
#include <net/udp.h>

#include <devices/pit.h>
#include <utilities/log.h>
#include <utilities/string.h>

#define DHCP_PACKET_SIZE 576
#define DHCP_MAX_RETRIES 5

static const u8 broadcast[4] = {255, 255, 255, 255};

static const u8 zero[4] = {0, 0, 0, 0};

static struct {
  enum dhcp_state state;

  struct dhcp_lease lease;
  struct dhcp_lease offered;

  u32 xid;
  u32 retries;

  u64 started;
  u64 deadline;
  u64 acquired;
  u64 renew_at;
  u64 rebind_at;
  u64 expires_at;

  int active;
} dhcp;

static u32 dhcp_read_u32(const u8 *p) {
  return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | ((u32)p[3]);
}

static int dhcp_parse_options(const u8 *p, u16 remaining, struct dhcp_lease *lease, u8 *message) {
  *message = 0;

  while (remaining) {
    u8 type = *p++;
    remaining--;

    if (type == DHCP_OPT_PAD)
      continue;

    if (type == DHCP_OPT_END)
      return *message ? 0 : -1;

    if (!remaining)
      return -1;

    u8 len = *p++;
    remaining--;

    if (len > remaining)
      return -1;

    switch (type) {
    case DHCP_OPT_TYPE:
      if (len != 1)
        return -1;

      *message = p[0];
      break;

    case DHCP_OPT_SUBNET:
      if (len != 4)
        return -1;

      memcpy(lease->netmask, p, 4);
      break;

    case DHCP_OPT_ROUTER:
      if (len < 4 || len % 4)
        return -1;

      memcpy(lease->gateway, p, 4);
      break;

    case DHCP_OPT_DNS:
      if (len < 4 || len % 4)
        return -1;

      lease->dns_count = len / 4;

      if (lease->dns_count > 4)
        lease->dns_count = 4;

      memcpy(lease->dns, p, lease->dns_count * sizeof(lease->dns[0]));
      break;

    case DHCP_OPT_SERVER:
      if (len != 4)
        return -1;

      memcpy(lease->server, p, 4);
      break;

    case DHCP_OPT_LEASE:
      if (len != 4)
        return -1;

      lease->duration = dhcp_read_u32(p);
      break;

    case DHCP_OPT_T1:
      if (len != 4)
        return -1;

      lease->renewal = dhcp_read_u32(p);
      break;

    case DHCP_OPT_T2:
      if (len != 4)
        return -1;

      lease->rebinding = dhcp_read_u32(p);
      break;
    }

    p += len;
    remaining -= len;
  }

  return -1;
}

static u64 dhcp_seconds(u32 seconds) {
  u32 hz = pit_get_freq();

  if (!hz)
    hz = 1000;

  return (u64)seconds * hz;
}

static u32 dhcp_new_xid(void) {
  struct netif *nif = netif_get();

  u32 xid = (u32)pit_ticks() ^ dhcp.xid;

  if (nif) {
    for (int i = 0; i < 6; i++)
      xid = (xid * 16777619u) ^ nif->mac[i];
  }

  return xid ? xid : 1;
}

enum dhcp_state dhcp_get_state(void) { return dhcp.state; }

const struct dhcp_lease *dhcp_get_lease(void) {
  if (dhcp.state < DHCP_BOUND)
    return NULL;

  return &dhcp.lease;
}

static void dhcp_apply_lease(struct dhcp_lease *lease) {
  int changed = !ipv4_addr_equal(dhcp.lease.address, lease->address) ||
                !ipv4_addr_equal(dhcp.lease.netmask, lease->netmask) ||
                !ipv4_addr_equal(dhcp.lease.gateway, lease->gateway);

  if (!lease->renewal || lease->renewal >= lease->duration) {

    lease->renewal = lease->duration / 2;
  }

  if (!lease->rebinding || lease->rebinding >= lease->duration ||
      lease->rebinding <= lease->renewal) {

    lease->renewal   = lease->duration / 2;
    lease->rebinding = ((u64)lease->duration * 7) / 8;
  }

  dhcp.lease    = *lease;
  dhcp.acquired = pit_ticks();

  dhcp.state   = DHCP_BOUND;
  dhcp.retries = 0;

  if (lease->duration == 0xffffffffu) {
    dhcp.renew_at   = ~(u64)0;
    dhcp.rebind_at  = ~(u64)0;
    dhcp.expires_at = ~(u64)0;
  } else {
    dhcp.renew_at = dhcp.acquired + dhcp_seconds(lease->renewal);

    dhcp.rebind_at = dhcp.acquired + dhcp_seconds(lease->rebinding);

    dhcp.expires_at = dhcp.acquired + dhcp_seconds(lease->duration);
  }

  if (changed)
    arp_flush();

  netif_set_ipv4(lease->address, lease->netmask, lease->gateway);

  if (changed) {
    arp_announce();
    arp_prime_gateway();
  }

  log_write_fmt(KERNEL,
                LOG_INFO,
                "dhcp: bound to %d.%d.%d.%d",
                lease->address[0],
                lease->address[1],
                lease->address[2],
                lease->address[3]);
  log_write_fmt(KERNEL,
                LOG_INFO,
                "dhcp: Netmask %d.%d.%d.%d",
                lease->netmask[0],
                lease->netmask[1],
                lease->netmask[2],
                lease->netmask[3]);
  log_write_fmt(KERNEL,
                LOG_INFO,
                "dhcp: Gateway %d.%d.%d.%d",
                lease->gateway[0],
                lease->gateway[1],
                lease->gateway[2],
                lease->gateway[3]);
  log_write_fmt(KERNEL,
                LOG_INFO,
                "dhcp: Server %d.%d.%d.%d",
                lease->server[0],
                lease->server[1],
                lease->server[2],
                lease->server[3]);
  for (u8 i = 0; i < lease->dns_count; i++) {
    log_write_fmt(KERNEL,
                  LOG_INFO,
                  "dhcp: DNS %d.%d.%d.%d",
                  lease->dns[i][0],
                  lease->dns[i][1],
                  lease->dns[i][2],
                  lease->dns[i][3]);
  }
  log_write_fmt(KERNEL, LOG_INFO, "dhcp: Lease duration %u seconds", lease->duration);
  log_write_fmt(KERNEL, LOG_INFO, "dhcp: Renew in %u seconds", lease->renewal);
  log_write_fmt(KERNEL, LOG_INFO, "dhcp: Rebind in %u seconds", lease->rebinding);
}

static u8 *dhcp_add_option(u8 *p, u8 type, u8 len, const void *data) {
  *p++ = type;
  *p++ = len;

  memcpy(p, data, len);

  return p + len;
}

static int dhcp_send(u8 type) {
  struct netif *nif = netif_get();

  if (!nif)
    return -1;

  u8 packet[DHCP_PACKET_SIZE];

  memset(packet, 0, sizeof(packet));

  struct dhcp_header *hdr = (struct dhcp_header *)packet;

  hdr->op     = 1;
  hdr->htype  = 1;
  hdr->hlen   = 6;
  hdr->xid    = to_be32(dhcp.xid);
  hdr->cookie = to_be32(DHCP_MAGIC_COOKIE);

  u32 hz      = pit_get_freq();
  u64 elapsed = hz ? (pit_ticks() - dhcp.started) / hz : 0;

  hdr->secs = to_be16(elapsed > 65535 ? 65535 : (u16)elapsed);

  memcpy(hdr->chaddr, nif->mac, 6);

  const u8 *destination = broadcast;

  if (dhcp.state == DHCP_RENEWING || dhcp.state == DHCP_REBINDING) {

    memcpy(hdr->ciaddr, dhcp.lease.address, 4);

    if (dhcp.state == DHCP_RENEWING)
      destination = dhcp.lease.server;
  } else {
    hdr->flags = to_be16(0x8000);
  }

  u8 *p = packet + sizeof(*hdr);

  p = dhcp_add_option(p, DHCP_OPT_TYPE, 1, &type);

  if (type == DHCP_REQUEST && dhcp.state == DHCP_REQUESTING) {

    p = dhcp_add_option(p, DHCP_OPT_REQUESTED, 4, dhcp.offered.address);

    p = dhcp_add_option(p, DHCP_OPT_SERVER, 4, dhcp.offered.server);
  }

  const u8 parameters[] = {
      DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS, DHCP_OPT_LEASE, DHCP_OPT_T1, DHCP_OPT_T2};

  p = dhcp_add_option(p, DHCP_OPT_PARAMS, sizeof(parameters), parameters);

  *p++ = DHCP_OPT_END;

  u16 len = (u16)(p - packet);

  // Pad short DHCP messages to the BOOTP minimum.
  if (len < 300)
    len = 300;

  return udp_output(destination, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, packet, len);
}

void dhcp_input(const u8 *payload, u16 len) {
  struct netif *nif = netif_get();

  if (!dhcp.active || !nif || len < sizeof(struct dhcp_header))
    return;

  const struct dhcp_header *hdr = (const struct dhcp_header *)payload;

  if (hdr->op != 2 || hdr->htype != 1 || hdr->hlen != 6)
    return;

  if (from_be32(hdr->cookie) != DHCP_MAGIC_COOKIE)
    return;

  if (from_be32(hdr->xid) != dhcp.xid)
    return;

  if (memcmp(hdr->chaddr, nif->mac, 6) != 0)
    return;

  struct dhcp_lease lease;
  memset(&lease, 0, sizeof(lease));

  u8 message;

  if (dhcp_parse_options(payload + sizeof(*hdr), len - sizeof(*hdr), &lease, &message) != 0)
    return;

  if (message == DHCP_OFFER) {
    if (dhcp.state != DHCP_SELECTING)
      return;

    if (ipv4_addr_is_zero(hdr->yiaddr) || ipv4_addr_is_zero(lease.server))
      return;

    memcpy(lease.address, hdr->yiaddr, 4);

    dhcp.offered = lease;
    dhcp.state   = DHCP_REQUESTING;
    dhcp.retries = 0;

    dhcp_send(DHCP_REQUEST);

    dhcp.deadline = pit_ticks() + dhcp_seconds(4);

    log_write("dhcp: offer accepted", KERNEL, LOG_INFO);

    return;
  }

  if (dhcp.state != DHCP_REQUESTING && dhcp.state != DHCP_RENEWING && dhcp.state != DHCP_REBINDING)
    return;

  if (ipv4_addr_is_zero(lease.server))
    return;

  if (dhcp.state == DHCP_REQUESTING && !ipv4_addr_equal(lease.server, dhcp.offered.server))
    return;

  if (dhcp.state == DHCP_RENEWING && !ipv4_addr_equal(lease.server, dhcp.lease.server))
    return;

  if (message == DHCP_NAK) {
    log_write("dhcp: lease rejected", KERNEL, LOG_INFO);

    dhcp_start();
    return;
  }

  if (message != DHCP_ACK)
    return;

  if (!lease.duration)
    return;

  memcpy(lease.address, hdr->yiaddr, 4);

  if (dhcp.state != DHCP_REQUESTING && ipv4_addr_is_zero(lease.address)) {

    memcpy(lease.address, dhcp.lease.address, 4);
  }

  if (ipv4_addr_is_zero(lease.address))
    return;

  if (dhcp.state == DHCP_REQUESTING && !ipv4_addr_equal(lease.address, dhcp.offered.address))
    return;

  dhcp_apply_lease(&lease);
}

void dhcp_start(void) {
  if (!netif_get())
    return;

  dhcp.xid = dhcp_new_xid();

  memset(&dhcp.lease, 0, sizeof(dhcp.lease));
  memset(&dhcp.offered, 0, sizeof(dhcp.offered));

  netif_set_ipv4(zero, zero, zero);
  arp_flush();

  dhcp.active  = 1;
  dhcp.state   = DHCP_SELECTING;
  dhcp.retries = 0;
  dhcp.started = pit_ticks();

  dhcp_send(DHCP_DISCOVER);

  dhcp.deadline = pit_ticks() + dhcp_seconds(4);

  log_write("dhcp: discovering servers", KERNEL, LOG_INFO);
}

void dhcp_stop(void) {
  dhcp.active = 0;
  dhcp.state  = DHCP_INIT;

  memset(&dhcp.lease, 0, sizeof(dhcp.lease));

  netif_set_ipv4(zero, zero, zero);
  arp_flush();
}

void dhcp_tick(void) {
  if (!dhcp.active || !netif_get())
    return;

  u64 now = pit_ticks();

  if (dhcp.state >= DHCP_BOUND && now >= dhcp.expires_at) {

    dhcp_start();
    return;
  }

  if (dhcp.state == DHCP_BOUND) {
    if (now < dhcp.renew_at)
      return;

    dhcp.state   = DHCP_RENEWING;
    dhcp.xid     = dhcp_new_xid();
    dhcp.started = now;

    dhcp_send(DHCP_REQUEST);

    dhcp.deadline = now + dhcp_seconds(4);
    return;
  }

  if (dhcp.state == DHCP_RENEWING && now >= dhcp.rebind_at) {

    dhcp.state = DHCP_REBINDING;

    dhcp_send(DHCP_REQUEST);

    dhcp.deadline = now + dhcp_seconds(4);
    return;
  }

  if (now < dhcp.deadline)
    return;

  switch (dhcp.state) {
  case DHCP_SELECTING:
  case DHCP_REQUESTING: {
    if (++dhcp.retries >= DHCP_MAX_RETRIES) {
      dhcp_start();
      return;
    }

    dhcp_send(dhcp.state == DHCP_SELECTING ? DHCP_DISCOVER : DHCP_REQUEST);

    dhcp.deadline = now + dhcp_seconds(4u << dhcp.retries);

    break;
  }

  case DHCP_RENEWING:
  case DHCP_REBINDING: {
    dhcp_send(DHCP_REQUEST);

    u64 remaining = (dhcp.state == DHCP_RENEWING ? dhcp.rebind_at : dhcp.expires_at) - now;

    u64 delay   = remaining / 2;
    u64 minimum = dhcp_seconds(60);

    if (delay < minimum)
      delay = minimum;

    dhcp.deadline = now + delay;
    break;
  }

  default:
    break;
  }
}
