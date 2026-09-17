#ifndef ARCH_IRQ_H
#define ARCH_IRQ_H

#include <utilities/types.h>

#if !defined(__x86_64__) && !defined(__i386__)
#error "irq.h requires an x86 target"
#endif

/* The interrupt-enable bit in x86 RFLAGS. */
#define RFLAGS_INTERRUPT_ENABLE (1ULL << 9)

/* Return the current RFLAGS without changing the interrupt state. */
SINLINE u64 irq_flags_read(void) {
  u64 rflags;

  __asm__ volatile("pushfq; popq %0" : "=r"(rflags));
  return rflags;
}

/* Disable maskable interrupts and return the previous RFLAGS. The returned
 * value is an opaque token for irq_restore(); it intentionally restores only
 * the interrupt-enable state, matching the old local implementations. */
SINLINE u64 irq_save(void) {
  u64 rflags;

  __asm__ volatile("pushfq; popq %0; cli" : "=r"(rflags) :: "memory");
  return rflags;
}

SINLINE void irq_restore(u64 rflags) {
  if (rflags & RFLAGS_INTERRUPT_ENABLE)
    __asm__ volatile("sti" ::: "memory");
}

SINLINE int interrupts_enabled(void) {
  return (irq_flags_read() & RFLAGS_INTERRUPT_ENABLE) != 0;
}

#endif /* ARCH_IRQ_H */
