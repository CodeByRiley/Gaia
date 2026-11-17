/* kernel/net/udp.c */
#include "net/udp.h"

#include "net/dhcp.h"
#include "net/ipv4.h"
#include "net/ksocket.h"
#include "net/netif.h"
#include "utilities/log.h"
#include "utilities/string.h"

/* Same walker as tcp.c: byte-pair wise so it is endian-neutral and
 * never does unaligned loads, odd tail padded. If the duplication
 * annoys, this is the function to hoist into inet.h. */
static u32 udp_sum(const u8 *p, u16 len, u32 sum) {
  u16 i;
  for (i = 0; i + 1 < len; i += 2)
    sum += ((u32)p[i] << 8) | p[i + 1];
  if (len & 1)
    sum += (u32)p[len - 1] << 8;
  return sum;
}

u16 udp_checksum(const u8 src_ip[4], const u8 dst_ip[4], const struct udp_header *hdr,
                 u16 seg_len) {
  struct udp_pseudo_header pseudo;
  memcpy(pseudo.src, src_ip, IPV4_ALEN);
  memcpy(pseudo.dst, dst_ip, IPV4_ALEN);
  pseudo.zero     = 0;
  pseudo.protocol = IPPROTO_UDP;
  pseudo.length   = to_be16(seg_len);

  u32 sum = udp_sum((const u8 *)&pseudo, (u16)sizeof pseudo, 0);
  sum     = udp_sum((const u8 *)hdr, seg_len, sum);
  while (sum >> 16)
    sum = (sum & 0xFFFFu) + (sum >> 16);
  return (u16)~sum;
}

void udp_input(const struct ipv4_hdr *ip, const u8 *segment, u16 seg_len) {
  log_write_fmt(KERNEL, LOG_INFO, "udp: input seg_len=%u", seg_len);

  if (seg_len < UDP_HEADER_SIZE) {
    log_write("udp: segment too short", KERNEL, LOG_INFO);
    return;
  }

  const struct udp_header *hdr = (const struct udp_header *)segment;
  u16 udp_len                  = from_be16(hdr->length);

  log_write_fmt(KERNEL,
                LOG_INFO,
                "udp: src=%u dst=%u len=%u checksum=%04x",
                from_be16(hdr->src_port),
                from_be16(hdr->dst_port),
                udp_len,
                from_be16(hdr->checksum));

  if (udp_len < UDP_HEADER_SIZE || udp_len > seg_len) {
    log_write("udp: invalid length", KERNEL, LOG_INFO);
    return;
  }

  u16 checksum = udp_checksum(ip->src, ip->dst, hdr, udp_len);

  if (hdr->checksum != 0 && checksum != 0) {
    log_write_fmt(KERNEL, LOG_INFO, "udp: bad checksum, residual=%04x", checksum);
    return;
  }

  log_write("udp: validation passed", KERNEL, LOG_INFO);
  const u8 *payload = segment + UDP_HEADER_SIZE;
  u16 payload_len   = (u16)(udp_len - UDP_HEADER_SIZE);

  struct ipv4_addr src = *(struct ipv4_addr *)ip->src;
  struct ipv4_addr dst = *(struct ipv4_addr *)ip->dst;

  if (from_be16(hdr->src_port) == DHCP_SERVER_PORT &&
      from_be16(hdr->dst_port) == DHCP_CLIENT_PORT) {

    dhcp_input(payload, payload_len);
    return;
  }

  socket_handle_incoming(
      src, hdr->src_port, dst, hdr->dst_port, IPPROTO_UDP, (void *)payload, payload_len);
}

int udp_output(const u8 dst_ip[IPV4_ALEN], u16 src_port, u16 dst_port, const void *payload,
               u16 payload_len) {
  if (payload_len > IPV4_PAYLOAD_MAX - UDP_HEADER_SIZE)
    return -1;

  /* The checksum covers a source address, and ipv4_output picks that
   * itself from the netif, so peek at the same one. Works pre-DHCP too:
   * 0.0.0.0 -> 255.255.255.255 checksums and routes as-is. */
  struct netif *nif = netif_get();
  if (!nif)
    return -1;

  u16 seg_len = (u16)(UDP_HEADER_SIZE + payload_len);
  u8 seg[IPV4_PAYLOAD_MAX]; /* same stack budget ipv4_output already spends */

  if (payload_len && payload)
    memcpy(seg + UDP_HEADER_SIZE, payload, payload_len);

  struct udp_header *udp = (struct udp_header *)seg;
  udp->src_port          = to_be16(src_port);
  udp->dst_port          = to_be16(dst_port);
  udp->length            = to_be16(seg_len);
  udp->checksum          = 0;
  udp->checksum          = to_be16(udp_checksum(nif->ipv4, dst_ip, udp, seg_len));
  /* A computed checksum of zero goes out as 0xFFFF: zero on the wire
   * means "unchecked", which is not what we computed (RFC 768). */
  if (udp->checksum == 0)
    udp->checksum = 0xFFFF;

  return ipv4_output(dst_ip, IPPROTO_UDP, seg, seg_len);
}
