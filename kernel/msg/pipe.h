/* kernel/msg/pipe.h , anonymous pipes.
 *
 * A pipe is a byte ring with two ends. Descriptors hold ends, not the pipe:
 * every descriptor that refers to the read end counts as one reader, every
 * one on the write end as one writer, whether it came from pipe(), dup() or
 * a spawned child inheriting its parent's stdio. The pipe is freed when both
 * counts reach zero.
 *
 * POSIX semantics, minus signals:
 *   - read blocks while the ring is empty and a writer remains; with no
 *     writers left an empty ring reads as end-of-file (0).
 *   - write blocks while the ring is full; with no readers left it fails
 *     with -EPIPE rather than raising SIGPIPE, which Gaia cannot deliver.
 *   - a write of at most PIPE_BUF bytes lands contiguously.
 *
 * Waiting uses sync/waitqueue.h, so the same IRQs-off rule applies: every
 * call here is BSP task context.
 */
#ifndef MSG_PIPE_H
#define MSG_PIPE_H

#include <sync/waitqueue.h>
#include <utilities/types.h>

#define PIPE_CAPACITY 4096U
/* The atomic-write limit POSIX requires, at its minimum. */
#define PIPE_BUF 512U

struct pipe {
  u8 data[PIPE_CAPACITY];
  u32 head;  /* next byte to read */
  u32 count; /* bytes buffered */
  u32 readers;
  u32 writers;
  struct wait_queue readable; /* readers parked on an empty ring */
  struct wait_queue writable; /* writers parked on a full ring */
};

/* A new pipe with one reader and one writer. NULL on allocation failure. */
struct pipe *pipe_create(void);

/* Another descriptor now refers to this end. */
void pipe_ref(struct pipe *p, int write_end);

/* A descriptor on this end went away. Wakes the other side so it can see
 * EOF or EPIPE, and frees the pipe with the last reference. */
void pipe_unref(struct pipe *p, int write_end);

/* Returns bytes copied, 0 at end-of-file, or -EAGAIN when `nonblock` and
 * the ring is empty. `buf` must be a validated user or kernel buffer. */
long pipe_read(struct pipe *p, void *buf, usize n, int nonblock);

/* Returns bytes written, which is all of `n` unless `nonblock` cut it
 * short, or -EPIPE / -EAGAIN when nothing could be written. */
long pipe_write(struct pipe *p, const void *buf, usize n, int nonblock);

/* For poll: data to read, or end-of-file, which also reads without
 * blocking. */
int pipe_readable(const struct pipe *p);
/* For poll: room to write, or no readers, which also returns at once. */
int pipe_writable(const struct pipe *p);

#endif
