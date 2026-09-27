#ifndef NET_TCP_H
#define NET_TCP_H

#include "net/inet.h"
#include "net/ipv4.h"
#include "net/ksocket.h"
#include <stdint.h>
#include <utilities/types.h>

// Config
#define TCP_WINDOW_SIZE 8192u
#define TCP_MSL_MS 120000u // Maximum Segment Lifetime
#define TCP_MIN_HEADER_LEN 20u
#define TCP_MAX_HEADER_LEN 60u
#define TCP_DATA_OFFSET_MIN 5u
#define TCP_DATA_OFFSET_MAX 15u

struct tcp_header {
  u16 src_port;   /* network byte order */
  u16 dst_port;   /* network byte order */
  u32 seq_num;    /* network byte order */
  u32 ack_num;    /* network byte order */
  u8 data_offset; /* high nibble: offset in 32-bit words */
  u8 flags;
  u16 win_size; /* network byte order */
  u16 checksum; /* network byte order */
  u16 urgent;   /* network byte order */
} PACKED;

static inline u8 tcp_header_len(const struct tcp_header *header) {
  return (u8)((header->data_offset >> 4) * 4u);
}

static inline bool tcp_header_len_valid(const struct tcp_header *header) {
  u8 words = header->data_offset >> 4;

  return words >= TCP_DATA_OFFSET_MIN && words <= TCP_DATA_OFFSET_MAX;
}

static inline bool tcp_seq_lt(u32 a, u32 b) { return (i32)(a - b) < 0; }

static inline bool tcp_seq_le(u32 a, u32 b) { return (i32)(a - b) <= 0; }

static inline bool tcp_seq_gt(u32 a, u32 b) { return (i32)(a - b) > 0; }

static inline bool tcp_seq_ge(u32 a, u32 b) { return (i32)(a - b) >= 0; }

// Flags
#define TCP_FIN 0x01u
#define TCP_SYN 0x02u
#define TCP_RST 0x04u
#define TCP_PSH 0x08u
#define TCP_ACK 0x10u
#define TCP_URG 0x20u
#define TCP_ECE 0x40u
#define TCP_CWR 0x80u

// Options
#define TCP_OPT_END 0u
#define TCP_OPT_NOP 1u
#define TCP_OPT_MSS 2u
#define TCP_OPT_SACK_PERMITTED 4u
#define TCP_OPT_TIMESTAMP 8u

struct tcp_options {
  u16 mss; /* host byte order */
  bool has_mss;

  bool sack_permitted;

  bool timestamp_present;
  u32 timestamp_value; /* host byte order */
  u32 timestamp_echo;  /* host byte order */
};

// TCP State
enum tcp_state {
  TCP_CLOSED = 0,
  TCP_LISTEN,
  TCP_SYN_SENT,
  TCP_SYN_RECEIVED,
  TCP_ESTABLISHED,
  TCP_FIN_WAIT_1,
  TCP_FIN_WAIT_2,
  TCP_CLOSE_WAIT,
  TCP_CLOSING,
  TCP_LAST_ACK,
  TCP_TIME_WAIT,
};

extern const char *g_tcpStateStrs[];

// TCP Errors
enum tcp_error {
  TCP_CONN_RESET = 1,
  TCP_CONN_REFUSED,
  TCP_CONN_CLOSING,
};

/* Common flag combinations for building responses. */
#define TCP_SYN_ACK (TCP_SYN | TCP_ACK)
#define TCP_FIN_ACK (TCP_FIN | TCP_ACK)
#define TCP_RST_ACK (TCP_RST | TCP_ACK)

/* RFC 1122: default MSS when the peer sends no MSS option. */
#define TCP_MSS_DEFAULT 536u

/* Checksum input only; never transmitted. */
struct tcp_pseudo_header {
  u8 src[4];   /* network byte order */
  u8 dst[4];   /* network byte order */
  u8 zero;
  u8 protocol; /* IPPROTO_TCP */
  u16 tcp_len; /* network byte order: header + payload */
} PACKED;

static inline bool tcp_flags_all(const struct tcp_header *h, u8 mask) {
  return (h->flags & mask) == mask;
}

static inline bool tcp_flags_any(const struct tcp_header *h, u8 mask) {
  return (h->flags & mask) != 0;
}

static inline void tcp_hdr_set_data_offset(struct tcp_header *h, u8 words) {
  h->data_offset = (u8)(words << 4);
}

/* Host-byte-order reads, so callers never touch the raw fields. */
static inline u16 tcp_hdr_src_port(const struct tcp_header *h) {
  return from_be16(h->src_port);
}

static inline u16 tcp_hdr_dst_port(const struct tcp_header *h) {
  return from_be16(h->dst_port);
}

static inline u32 tcp_hdr_seq(const struct tcp_header *h) {
  return from_be32(h->seq_num);
}

static inline u32 tcp_hdr_ack(const struct tcp_header *h) {
  return from_be32(h->ack_num);
}

static inline u16 tcp_hdr_window(const struct tcp_header *h) {
  return from_be16(h->win_size);
}

/* SYN and FIN each consume one sequence number despite carrying no data. */
static inline u32 tcp_seg_seq_len(const struct tcp_header *h,
                                  u16 payload_len) {
  return (u32)payload_len + (tcp_flags_any(h, TCP_SYN) ? 1u : 0u) +
         (tcp_flags_any(h, TCP_FIN) ? 1u : 0u);
}

/* Checksum over the pseudo-header plus the whole segment.
 *   To send:  zero hdr->checksum, then store the return value in it.
 *   To check: pass the segment exactly as received, checksum field
 *             included; a valid segment sums to 0. */
u16 tcp_checksum(const u8 src_ip[4], const u8 dst_ip[4],
                 const struct tcp_header *hdr, u16 seg_len);

/* Walks the option bytes after the fixed 20-byte header into `out`.
 * Unknown options are skipped by length; anything malformed stops the walk. */
void tcp_parse_options(const u8 *opts, u8 opts_len, struct tcp_options *out);

/* Entry point from ipv4_input(): `segment` is the whole TCP segment. */
void tcp_input(const struct ipv4_hdr *ip, const u8 *segment, u16 seg_len);

void tcp_init(void);

#endif
