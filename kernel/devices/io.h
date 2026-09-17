/* kernel/devices/io.h , port I/O primitives.
 *
 * inb/outb + 16/32-bit variants plus an `io_wait()` idiom. All static
 * inlines so call sites get folded into the right asm instructions and
 * no .c file is needed.
 */
#ifndef IO_H
#define IO_H

#include <arch/irq.h>
#include <stdint.h>
#include <utilities/types.h>

SINLINE void outb(u16 port, u8 val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

SINLINE u8 inb(u16 port) {
  u8 ret;
  __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
  return ret;
}

SINLINE void outw(u16 port, u16 val) {
  __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

SINLINE u16 inw(u16 port) {
  u16 ret;
  __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
  return ret;
}

SINLINE void outl(u16 port, u32 val) {
  __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

SINLINE u32 inl(u16 port) {
  u32 ret;
  __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
  return ret;
}

/* "Wait one ISA bus cycle" , write to BIOS POST diagnostic port 0x80,
 * which is unused on modern systems and has no side effects. Legacy
 * idiom for spacing out back-to-back PIC operations etc. */
SINLINE void io_wait(void) { outb(0x80, 0); }

#endif
