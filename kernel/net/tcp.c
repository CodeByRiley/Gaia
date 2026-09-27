#include "net/tcp.h"

#include "net/ipv4.h"
#include "net/ksocket.h"
#include "utilities/log.h"
#include "utilities/string.h"

const char *g_tcpStateStrs[] = {
    "CLOSED",      "LISTEN",     "SYN_SENT",   "SYN_RECEIVED",
    "ESTABLISHED", "FIN_WAIT_1", "FIN_WAIT_2", "CLOSE_WAIT",
    "CLOSING",     "LAST_ACK",   "TIME_WAIT",
};

void tcp_init(void) {
  /* Retransmit timers and TCB tables go here. */
}

/* RFC 1071 one's-complement sum over big-endian 16-bit words, done
 * byte-pair by byte-pair so it does not depend on host endianness.
 * A trailing odd byte counts as a word padded with zero. */
static u32 tcp_sum(const u8 *p, u16 len, u32 sum) {
  u16 i;
  for (i = 0; i + 1 < len; i += 2)
    sum += ((u32)p[i] << 8) | p[i + 1];
  if (len & 1)
    sum += (u32)p[len - 1] << 8;
  return sum;
}

u16 tcp_checksum(const u8 src_ip[4], const u8 dst_ip[4],
                 const struct tcp_header *hdr, u16 seg_len) {
  struct tcp_pseudo_header pseudo;
  memcpy(pseudo.src, src_ip, IPV4_ALEN);
  memcpy(pseudo.dst, dst_ip, IPV4_ALEN);
  pseudo.zero = 0;
  pseudo.protocol = IPPROTO_TCP;
  pseudo.tcp_len = to_be16(seg_len);

  u32 sum = tcp_sum((const u8 *)&pseudo, (u16)sizeof pseudo, 0);
  sum = tcp_sum((const u8 *)hdr, seg_len, sum);

  while (sum >> 16)
    sum = (sum & 0xFFFFu) + (sum >> 16);
  return (u16)~sum;
}

void tcp_parse_options(const u8 *opts, u8 opts_len, struct tcp_options *out) {
  memset(out, 0, sizeof *out);

  u8 i = 0;
  while (i < opts_len) {
    u8 kind = opts[i];

    if (kind == TCP_OPT_END)
      break; /* everything after END is padding */
    if (kind == TCP_OPT_NOP) {
      i++;
      continue;
    }

    /* Every real option carries a length byte. Without one, or with a
     * length that overruns the header, stop instead of reading garbage. */
    if (i + 1 >= opts_len)
      break;
    u8 len = opts[i + 1];
    if (len < 2 || len > (u8)(opts_len - i))
      break;

    switch (kind) {
    case TCP_OPT_MSS:
      if (len == 4) {
        u16 raw;
        memcpy(&raw, opts + i + 2, sizeof raw); /* options can be unaligned */
        out->mss = from_be16(raw);
        out->has_mss = true;
      }
      break;
    case TCP_OPT_SACK_PERMITTED:
      if (len == 2)
        out->sack_permitted = true;
      break;
    case TCP_OPT_TIMESTAMP:
      if (len == 10) {
        u32 raw[2];
        memcpy(raw, opts + i + 2, sizeof raw);
        out->timestamp_value = from_be32(raw[0]);
        out->timestamp_echo = from_be32(raw[1]);
        out->timestamp_present = true;
      }
      break;
    default:
      break; /* skip unknown options by length */
    }

    i += len;
  }
}

void tcp_input(const struct ipv4_hdr *ip, const u8 *segment, u16 seg_len) {
  if (seg_len < TCP_MIN_HEADER_LEN)
    return;

  const struct tcp_header *hdr = (const struct tcp_header *)segment;
  if (!tcp_header_len_valid(hdr))
    return;

  u16 hdr_len = tcp_header_len(hdr);
  if (hdr_len > seg_len)
    return;

  /* Verify before anything else touches the segment: a correct segment
   * sums to 0 with its own checksum field included. */
  if (tcp_checksum(ip->src, ip->dst, hdr, seg_len) != 0) {
    log_write("tcp: dropping segment with bad checksum", KERNEL, LOG_DEBUG);
    return;
  }

  struct tcp_options opts;
  tcp_parse_options(segment + TCP_MIN_HEADER_LEN,
                    (u8)(hdr_len - TCP_MIN_HEADER_LEN), &opts);

  const u8 *payload = segment + hdr_len;
  u16 payload_len = (u16)(seg_len - hdr_len);

  log_write_fmt(KERNEL, LOG_DEBUG,
                "tcp: %u.%u.%u.%u:%u -> local:%u flags=0x%02x seq=%u ack=%u "
                "win=%u len=%u mss=%u\n",
                ip->src[0], ip->src[1], ip->src[2], ip->src[3],
                tcp_hdr_src_port(hdr), tcp_hdr_dst_port(hdr), hdr->flags,
                tcp_hdr_seq(hdr), tcp_hdr_ack(hdr), tcp_hdr_window(hdr),
                payload_len, opts.has_mss ? opts.mss : 0u);

  /* Interim demux, same shape as the UDP path. A real TCP layer wants a
   * 4-tuple lookup with a fallback to listening sockets, and an RST when
   * nothing matches (RFC 793 §3.4). */
  struct ipv4_addr src = *(struct ipv4_addr *)ip->src;
  struct ipv4_addr dst = *(struct ipv4_addr *)ip->dst;
  socket_handle_incoming(src, hdr->src_port, dst, hdr->dst_port, IPPROTO_TCP,
                         (void *)payload, payload_len);
}
