/* kernel/devices/pit.c , 8254 PIT driver.
 *
 * Channel 0 fires IRQ0 at the requested frequency. The handler bumps the
 * global tick counter, charges the current task for the slice, and walks
 * sleepers. The common IRQ dispatcher considers ring-3 preemption only
 * after this handler returns and IRQ0 has been acknowledged.
 */
#include <devices/io.h>
#include <devices/pit.h>
#include <interrupts/idt.h>
#include <sched/sched.h>

#define PIT_CH0 0x40
#define PIT_CH1 0x41
#define PIT_CH2 0x42
#define PIT_CMD 0x43
/* NMI/keyboard ctrl port: bit0 = ch2 gate, bit5 = OUT2 */
#define PIT_GATE2 0x61
#define PIT_FREQ 1193182 /* 8254 input clock , divides down */

static volatile u64 ticks = 0;
static u32 current_freq_hz = 0;

/* IRQ0 handler. Kept cheap on purpose. */
static void pit_handler(void) {
  ticks++;
  struct task *cur = task_current();
  if (cur)
    cur->ticks_run++;
  sched_wake_sleepers();
}

/* Program PIT channel 0 to fire at `freq_hz`. */
void pit_init(u32 freq_hz) {
  if (freq_hz == 0)
    freq_hz = 100;

  u32 div = PIT_FREQ / freq_hz;

  if (div < 1)
    div = 1;
  if (div > 0xFFFF)
    div = 0xFFFF;

  current_freq_hz = PIT_FREQ / div;

  outb(PIT_CMD, 0x36);
  outb(PIT_CH0, (u8)(div & 0xff));
  outb(PIT_CH0, (u8)(div >> 8));

  irq_install(0, pit_handler);
}

u64 pit_ticks(void) { return ticks; }

/* This is the frequency requested of channel 0 by its divisor, not a sample
 * of handler entries. A legacy PIC can merge timer edges while one is pending,
 * and QEMU TCG makes that amount vary with host scheduling. Turning one such
 * sample into the kernel's time scale makes time depend on the boot. */
u32 pit_get_freq(void) { return current_freq_hz; }

/* Polled one-shot delay on channel 2.
 *
 * The tick counter is useless before the boot-time sti: IRQ0 never fires, so
 * anything waiting on pit_ticks() to advance waits forever. Device bring-up
 * (PCI, USB controller resets) all runs in that window and still needs real
 * wall-clock delays, so time them off channel 2 instead , it drives no
 * interrupt, only the OUT2 status bit, which we can poll with IF=0.
 *
 * Channel 2 is otherwise only wired to the PC speaker, which we never use;
 * the gate/speaker byte is saved and restored so we stay silent either way. */
void pit_delay_us(u64 us) {
  while (us) {
    /* 16-bit downcounter off the 1.193182 MHz clock, so one shot tops out at
     * 65535 ticks (~54.9 ms). Stay under that with room to spare. */
    u64 chunk = (us > 50000) ? 50000 : us;
    u32 count = (u32)((chunk * PIT_FREQ) / 1000000);
    if (count == 0)
      count = 1;

    u8 gate = inb(PIT_GATE2);
    /* gate on (bit 0), speaker data disconnected (bit 1) */
    outb(PIT_GATE2, (u8)((gate & ~0x02) | 0x01));

    outb(PIT_CMD, 0xB0); /* channel 2, lo/hi byte, mode 0, binary */
    outb(PIT_CH2, (u8)(count & 0xFF));
    outb(PIT_CH2, (u8)((count >> 8) & 0xFF));

    /* Mode 0 raises OUT2 at terminal count. */
    while (!(inb(PIT_GATE2) & 0x20))
      ;

    outb(PIT_GATE2, gate);
    us -= chunk;
  }
}

void pit_delay_ms(u32 ms) { pit_delay_us((u64)ms * 1000); }
