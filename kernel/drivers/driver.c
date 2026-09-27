/* kernel/drivers/driver.c - minimal device/driver registry. */
#include <arch/irq.h>
#include <devices/pit.h>
#include <drivers/driver.h>
#include <sched/sched.h>
#include <sync/waitqueue.h>
#include <stddef.h>
#include <stdint.h>
#include <utilities/log.h>

#define DRIVER_MAX_DRIVERS 16
#define DRIVER_MAX_DEVICES 64

static const struct driver *drivers[DRIVER_MAX_DRIVERS];
static struct device devices[DRIVER_MAX_DEVICES];
static u32 driver_count;
static u32 device_count;
static int pci_devices_probed;
static int poll_task_started;

/* How long the poll task sleeps with nothing to do. It is the resolution of
 * the timers that ride the poll pass , ARP retransmits (1 s) and link state
 * , not a receive latency: a device with an interrupt kicks the task awake
 * through driver_poll_kick the moment it has work. */
#define DRIVER_IDLE_MS 100U

static struct wait_queue poll_wq = WAIT_QUEUE_INIT;
/* Set by a kick, cleared by the task just before a pass, so a kick that
 * lands during a pass makes the task go round again instead of sleeping
 * through it. Read and cleared with IRQs off. */
static volatile int poll_kicked;

void driver_poll_kick(void) {
  poll_kicked = 1;
  wq_wake_all(&poll_wq);
}

/* The poll task used to end each pass with task_yield(), which returns at
 * once when nothing else is runnable , a tight spin that starved the guest
 * badly enough under TCG to drop PIT interrupts. It then slept one tick per
 * idle pass, which still cost a wakeup every tick and put up to a tick of
 * latency on every received frame.
 *
 * Now it parks on poll_wq. A driver's IRQ handler acknowledges its device
 * and calls driver_poll_kick; the work itself , protocol input, replies,
 * logging , stays here in task context, where it can allocate and take
 * locks. A receive burst still drains at full speed: a pass that handled
 * something yields rather than sleeps. */
static void driver_poll_thread(void) {
  int busy = 0;
  for (;;) {
    int worked = 0;

    for (u32 i = 0; i < device_count; i++) {
      struct device *device = &devices[i];
      if (device->driver && device->driver->poll) {
        worked |= device->driver->poll(device);
      }
    }

    if (worked && busy < 8) {
      busy++;
      task_yield();
      continue;
    }
    busy = 0;

    u32 freq = pit_get_freq();
    u64 idle_ticks = ((u64)DRIVER_IDLE_MS * (freq ? freq : 100) + 999) / 1000;
    u64 flags = irq_save();
    if (!poll_kicked)
      wq_wait(&poll_wq, pit_ticks() + (idle_ticks ? idle_ticks : 1));
    poll_kicked = 0;
    irq_restore(flags);
  }
}

static void ensure_poll_task(void) {
  if (poll_task_started)
    return;

  struct task *task = task_spawn(driver_poll_thread);
  if (!task) {
    log_write("DRIVER: could not spawn poll task", KERNEL, LOG_WARN);
    return;
  }

  task_set_name(task, "drivers");
  poll_task_started = 1;
  log_write("DRIVER: poll task spawned", KERNEL, LOG_INFO);
}

static int bind_device(struct device *device) {
  if (device->driver)
    return 1;

  for (u32 i = 0; i < driver_count; i++) {
    const struct driver *driver = drivers[i];
    if (driver->bus != device->bus)
      continue;
    if (!driver->match(device))
      continue;

    log_write_string("DRIVER: probing", driver->name, KERNEL, LOG_INFO);

    if (driver->probe(device) != 0) {
      log_write_string("DRIVER: probe failed", driver->name, KERNEL, LOG_WARN);
      continue;
    }
    log_write_string("DRIVER: probe success", driver->name, KERNEL, LOG_INFO);
    device->driver = driver;
    /* A successful probe means the device is operational. Keep this
     * explicit so diagnostics can distinguish a registered driver from
     * one that actually owns working hardware. */
    device->enabled = 1;
    if (driver->poll)
      ensure_poll_task();
    log_write_string("DRIVER: bound", driver->name, KERNEL, LOG_INFO);
    return 1;
  }

  /* Most enumerated devices will remain unbound until their drivers are
   * implemented. */
  return 0;
}

static int unbind_device(struct device *device) {
	if (!device->driver)
		return 1; // shit doesn't exist

	for (u32 i = 0; i < driver_count; i++) {
		const struct driver *drvr = drivers[i];

	}

	return 0;
}

void driver_core_init(void) {
  driver_count = 0;
  device_count = 0;
  pci_devices_probed = 0;
  poll_task_started = 0;
}

int driver_register(const struct driver *driver) {
  if (!driver || !driver->name || !driver->match || !driver->probe)
    return -1;
  if (driver->bus == DEVICE_BUS_NONE)
    return -1;
  if (driver_count >= DRIVER_MAX_DRIVERS) {
    log_write("DRIVER: registry full", KERNEL, LOG_ERROR);
    return -1;
  }

  drivers[driver_count++] = driver;
  log_write_string("DRIVER: registered", driver->name, KERNEL, LOG_INFO);

  /* Registration order is deliberately flexible: a driver registered after
   * bus enumeration still gets a chance to claim every unbound device. */
  for (u32 i = 0; i < device_count; i++)
    bind_device(&devices[i]);

  return 0;
}

int driver_register_isa_device(u16 io_base, u8 irq) {
  if (device_count >= DRIVER_MAX_DEVICES)
    return -1;
  log_write("DRIVER: registering device", KERNEL, LOG_INFO);

  struct device *device = &devices[device_count];
  device->bus = DEVICE_BUS_ISA;
  device->enabled = 0;
  device->driver = 0;
  device->driver_data = 0;
  device->bus_info.isa.io_base = io_base;
  device->bus_info.isa.irq = irq;

  device_count++;
  log_write_hex("DRIVER: device registered ", device_count, KERNEL, LOG_INFO);
  bind_device(device);
  return 0;
}

int driver_unregister() { return 0; };

int driver_probe_pci_devices(void) {
  u32 count = pci_device_count();
  if (count == 0)
    return 0; /* not enumerated yet, or genuinely empty */
  if (pci_devices_probed)
    return 0;
  pci_devices_probed = 1;
  u32 imported = 0;

  for (u32 i = 0; i < count; i++) {
    if (device_count >= DRIVER_MAX_DEVICES) {
      log_write("DRIVER: device table full", KERNEL, LOG_ERROR);
      break;
    }

    struct device *device = &devices[device_count];
    if (!pci_device_at(i, &device->bus_info.pci))
      continue;

    device->bus = DEVICE_BUS_PCI;
    device->enabled = 0;
    device->driver = 0;
    device->driver_data = 0;

    device_count++;
    imported++;
    bind_device(device);
  }

  log_write_hex("DRIVER: PCI devices imported =", imported, KERNEL, LOG_INFO);
  return (int)imported;
}

u32 driver_device_count(void) { return device_count; }

const struct device *driver_device_at(u32 index) {
  if (index >= device_count)
    return 0;
  return &devices[index];
}

int driver_snapshot(struct driver_snap *out, int max) {
  if (!out || max <= 0)
    return 0;

  int n = 0;
  for (u32 i = 0; i < driver_count && n < max; i++) {
    const struct driver *drv = drivers[i];
    if (!drv)
      continue;

    struct driver_snap *s = &out[n];
    s->bus = (int)drv->bus;
    s->poll = drv->poll ? 1 : 0;
    s->bound_devices = 0;
    s->enabled = 0;

    const char *name = drv->name ? drv->name : "unnamed";
    usize k = 0;
    while (k + 1 < DRIVER_SNAP_NAME_MAX && name[k]) {
      s->name[k] = name[k];
      k++;
    }
    s->name[k] = '\0';

    for (u32 j = 0; j < device_count; j++) {
      const struct device *d = &devices[j];
      if (d->driver == drv) {
        s->bound_devices++;
        if (d->enabled)
          s->enabled = 1;
      }
    }
    n++;
  }

  return n;
}
