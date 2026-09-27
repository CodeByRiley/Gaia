/* kernel/sync/waitqueue.c , see waitqueue.h. */
#include <sync/waitqueue.h>
#include <arch/irq.h>
#include <sched/sched.h>
#include <utilities/errno.h>
#include <utilities/panic.h>

void wq_add(struct wait_queue *wq, struct wq_entry *e) {
  struct task *t = task_current();
  if (!t)
    panic("wq_add: no current task");

  e->task = t;
  e->wq = wq;
  e->prev = 0;
  e->next = wq->head;
  if (wq->head)
    wq->head->prev = e;
  wq->head = e;

  e->task_next = t->wait_entries;
  t->wait_entries = e;
}

void wq_remove_all(struct task *t) {
  struct wq_entry *e = t->wait_entries;
  while (e) {
    struct wq_entry *next = e->task_next;
    if (e->prev)
      e->prev->next = e->next;
    else
      e->wq->head = e->next;
    if (e->next)
      e->next->prev = e->prev;
    e->prev = e->next = 0;
    e->task_next = 0;
    e = next;
  }
  t->wait_entries = 0;
}

int wq_wait(struct wait_queue *wq, u64 deadline) {
  struct wq_entry e;
  wq_add(wq, &e);
  int rc = task_block_until(deadline);
  wq_remove_all(task_current());
  return rc;
}

void wq_wake_all(struct wait_queue *wq) {
  u64 flags = irq_save();
  for (struct wq_entry *e = wq->head; e; e = e->next)
    task_wakeup(e->task);
  irq_restore(flags);
}
