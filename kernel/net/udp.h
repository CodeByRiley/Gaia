#ifndef NET_UDP_H
#define NET_UDP_H

#include <stdint.h>
#include "net/inet.h"
#include "net/ipv4.h"
#include "net/ksocket.h" // Included for struct ipv4_addr and struct packet_queue
#include <utilities/types.h>

#define UDP_HEADER_SIZE 8

struct udp_header {
  u16 src_port; /* network byte order */
  u16 dst_port; /* network byte order */
  u16 length;   /* network byte order: header + payload */
  u16 checksum; /* network byte order */
} PACKED;

/* Checksum input only; never transmitted. Byte-identical to the TCP
 * pseudo-header — RFC 768 just reuses the RFC 793 construction. */
struct udp_pseudo_header {
  u8 src[4];   /* network byte order */
  u8 dst[4];   /* network byte order */
  u8 zero;
  u8 protocol; /* IPPROTO_UDP */
  u16 length;  /* network byte order: header + payload */
} PACKED;

/* Host byte order on the ports, converted here, so callers can pass
 * literals (68, 67, 53) directly. */
int udp_output(const u8 dst_ip[IPV4_ALEN], u16 src_port, u16 dst_port,
               const void *payload, u16 payload_len);

/* RFC 1071 over pseudo-header + datagram. To verify: pass the datagram
 * exactly as received, checksum field included; a good one sums to 0. */
u16 udp_checksum(const u8 src_ip[IPV4_ALEN], const u8 dst_ip[IPV4_ALEN],
                 const struct udp_header *hdr, u16 seg_len);

/* Entry point from ipv4_input(): `segment` is the whole UDP datagram. */
void udp_input(const struct ipv4_hdr *ip, const u8 *segment, u16 seg_len);

#endif
