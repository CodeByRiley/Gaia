/* kernel/msg/pipe.c , see pipe.h. */
#include <msg/pipe.h>
#include <arch/irq.h>
#include <memory/heap.h>
#include <utilities/errno.h>
#include <utilities/string.h>

struct pipe *pipe_create(void) {
  struct pipe *p = (struct pipe *)kmalloc(sizeof(*p));
  if (!p)
    return NULL;
  memset(p, 0, sizeof(*p));
  p->readers = 1;
  p->writers = 1;
  wq_init(&p->readable);
  wq_init(&p->writable);
  return p;
}

void pipe_ref(struct pipe *p, int write_end) {
  u64 flags = irq_save();
  if (write_end)
    p->writers++;
  else
    p->readers++;
  irq_restore(flags);
}

void pipe_unref(struct pipe *p, int write_end) {
  u64 flags = irq_save();
  if (write_end) {
    if (p->writers && --p->writers == 0)
      wq_wake_all(&p->readable); /* readers now see EOF */
  } else {
    if (p->readers && --p->readers == 0)
      wq_wake_all(&p->writable); /* writers now see EPIPE */
  }
  int dead = p->readers == 0 && p->writers == 0;
  irq_restore(flags);

  /* Nobody can be parked here: a parked task holds a reference to the end
   * it waits on, and a killed one was unlinked before its references were
   * dropped. */
  if (dead)
    kfree(p);
}

int pipe_readable(const struct pipe *p) {
  return p->count > 0 || p->writers == 0;
}

int pipe_writable(const struct pipe *p) {
  return p->count < PIPE_CAPACITY || p->readers == 0;
}

long pipe_read(struct pipe *p, void *buf, usize n, int nonblock) {
  if (n == 0)
    return 0;

  u64 flags = irq_save();
  while (p->count == 0) {
    if (p->writers == 0) {
      irq_restore(flags);
      return 0;
    }
    if (nonblock) {
      irq_restore(flags);
      return -EAGAIN;
    }
    if (wq_wait(&p->readable, 0) == -EINTR) {
      irq_restore(flags);
      return -EINTR;
    }
  }

  usize chunk = n < p->count ? n : p->count;
  usize first = PIPE_CAPACITY - p->head;
  if (first > chunk)
    first = chunk;
  memcpy(buf, p->data + p->head, first);
  memcpy((u8 *)buf + first, p->data, chunk - first);
  p->head = (u32)((p->head + chunk) % PIPE_CAPACITY);
  p->count -= (u32)chunk;

  wq_wake_all(&p->writable);
  irq_restore(flags);
  return (long)chunk;
}

long pipe_write(struct pipe *p, const void *buf, usize n, int nonblock) {
  if (n == 0)
    return 0;

  u64 flags = irq_save();
  usize written = 0;
  long rc = 0;
  while (written < n) {
    if (p->readers == 0) {
      rc = -EPIPE;
      break;
    }

    /* A write of at most PIPE_BUF bytes waits until it fits whole, so it is
     * never interleaved with another writer's. Longer writes go in pieces. */
    usize space = PIPE_CAPACITY - p->count;
    usize need = (n <= PIPE_BUF) ? n - written : 1;
    if (space < need) {
      if (nonblock) {
        rc = -EAGAIN;
        break;
      }
      if (wq_wait(&p->writable, 0) == -EINTR) {
        rc = -EINTR;
        break;
      }
      continue;
    }

    usize chunk = n - written < space ? n - written : space;
    u32 tail = (p->head + p->count) % PIPE_CAPACITY;
    usize first = PIPE_CAPACITY - tail;
    if (first > chunk)
      first = chunk;
    memcpy(p->data + tail, (const u8 *)buf + written, first);
    memcpy(p->data, (const u8 *)buf + written + first, chunk - first);
    p->count += (u32)chunk;
    written += chunk;

    wq_wake_all(&p->readable);
  }
  irq_restore(flags);

  /* Partial progress is success; the error only stands when nothing went. */
  return written ? (long)written : rc;
}
