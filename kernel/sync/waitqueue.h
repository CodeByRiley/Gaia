/* kernel/sync/waitqueue.h , park a task until an object changes state.
 *
 * A wait queue is the list of tasks sleeping on one object: a socket's
 * receive queue, and later a pipe or a child's exit. The object's producer
 * calls wq_wake_all when the state changes; each woken task re-checks the
 * condition it was waiting for and parks again if it lost the race.
 *
 * Entries live on the waiter's kernel stack. A task can hold several at
 * once , poll parks on every descriptor it was handed , so each task keeps
 * a chain of the entries it owns, and wq_remove_all drops the lot. The
 * scheduler calls it when a parked task is killed, which is what stops a
 * reaped kernel stack being left linked into a live queue.
 *
 * Exclusion is interrupts-off, the same rule as the scheduler itself:
 * userspace and every waker run on the BSP, so disabling IRQs makes the
 * condition check, the enqueue and the block one atomic step. The pattern
 * every caller follows:
 *
 *     u64 flags = irq_save();
 *     while (!condition) {
 *       if (wq_wait(&obj->wq, deadline) != 0)
 *         break;                          // timed out
 *     }
 *     irq_restore(flags);
 *
 * A producer can call wq_wake_all from IRQ context. It never sleeps, and it
 * only moves tasks onto the ready queue; nobody runs until the next switch.
 */
#ifndef WAITQUEUE_H
#define WAITQUEUE_H

#include <utilities/types.h>

struct task;
struct wait_queue;

struct wq_entry {
  struct task *task;
  struct wait_queue *wq;
  struct wq_entry *prev;
  struct wq_entry *next;
  /* The owning task's other entries. See wq_remove_all. */
  struct wq_entry *task_next;
};

struct wait_queue {
  struct wq_entry *head;
};

#define WAIT_QUEUE_INIT {0}

SINLINE void wq_init(struct wait_queue *wq) { wq->head = 0; }

SINLINE int wq_empty(const struct wait_queue *wq) { return wq->head == 0; }

/* Link `e` into `wq` on behalf of the current task. IRQs must be off, and
 * stay off until the matching wq_remove_all. */
void wq_add(struct wait_queue *wq, struct wq_entry *e);

/* Unlink every entry `t` holds, on any queue. Safe on a task that holds
 * none. IRQs must be off. */
void wq_remove_all(struct task *t);

/* Park the current task on `wq` until a wake or, when `deadline` is
 * non-zero, until pit_ticks() reaches it. Returns 0 when woken and
 * -ETIMEDOUT when the deadline passed. Either way the entry is gone on
 * return. IRQs must be off; they are still off afterwards. */
int wq_wait(struct wait_queue *wq, u64 deadline);

/* Make every task parked on `wq` runnable. Their entries stay linked until
 * each one resumes and removes them, so a second wake before then is a
 * harmless no-op. Callable from IRQ context. */
void wq_wake_all(struct wait_queue *wq);

#endif
