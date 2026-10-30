#include <drivers/base/vendors/pci_ids.h>
#include <drivers/char/virtio/virtio_con.h>
#include <drivers/driver.h>
#include <drivers/virtio/virtio.h>
#include <memory/hhdm.h>
#include <memory/pmm.h>
#include <memory/vmm.h>
#include <pci/pci.h>
#include <utilities/log.h>
#include <utilities/string.h>

#define CON_CTRL_RX_SIZE 256
#define CLIPBOARD_RX_SIZE 4096

static struct virtio_dev vdev;

static int console_initialized    = 0;
static int clipboard_initialized  = 0;
static int control_tx_busy        = 0;
static int port_ready_pending     = 0;
static int clipboard_open_pending = 0;
static int clipboard_host_open    = 0;

/* Port zero */
static struct virtq port0_rx;
static struct virtq port0_tx;

/* Multipport control queues */
static struct virtq control_rx;
static struct virtq control_tx;

static struct virtq clipboard_rx;
static struct virtq clipboard_tx;

static u64 clipboard_rx_phys;
static u8 *clipboard_rx_buf;

static u64 ctrl_rx_phys;
static u64 ctrl_tx_phys;

static u8 *ctrl_rx_buf;
static u8 *ctrl_tx_buf;

struct virtio_console_control {
  u32 id;
  u16 event;
  u16 value;
} PACKED;

static int console_alloc_control_buffers(void) {
  ctrl_rx_phys = pmm_alloc_frame();
  ctrl_tx_phys = pmm_alloc_frame();

  if (!ctrl_rx_phys || !ctrl_tx_phys) {
    log_write("virtio-console: control buffer allocation failed", KERNEL, LOG_ERROR);
    return -1;
  }

  ctrl_rx_buf = phys_to_virt(ctrl_rx_phys);
  ctrl_tx_buf = phys_to_virt(ctrl_tx_phys);

  memset(ctrl_rx_buf, 0, 4096);
  memset(ctrl_tx_buf, 0, 4096);

  return 0;
}

static int console_post_control_rx(void) {
  u16 desc = virtq_alloc_desc(&control_rx);

  if (desc == 0xFFFF)
    return -1;

  control_rx.desc[desc].addr  = ctrl_rx_phys;
  control_rx.desc[desc].len   = CON_CTRL_RX_SIZE;
  control_rx.desc[desc].flags = VIRTQ_DESC_F_WRITE;
  control_rx.desc[desc].next  = 0;

  virtq_submit(&control_rx, desc);
  virtio_queue_notify(&vdev, &control_rx);

  return 0;
}

static int console_post_clipboard_rx(void) {
  u16 desc = virtq_alloc_desc(&clipboard_rx);

  if (desc == 0xFFFF)
    return -1;

  clipboard_rx.desc[desc].addr  = clipboard_rx_phys;
  clipboard_rx.desc[desc].len   = CLIPBOARD_RX_SIZE;
  clipboard_rx.desc[desc].flags = VIRTQ_DESC_F_WRITE;
  clipboard_rx.desc[desc].next  = 0;

  virtq_submit(&clipboard_rx, desc);
  virtio_queue_notify(&vdev, &clipboard_rx);

  return 0;
}

static int console_send_control(u32 id, u16 event, u16 value) {
  if (control_tx_busy)
    return -1;
  u16 desc = virtq_alloc_desc(&control_tx);
  if (desc == 0xFFFF)
    return -1;
  struct virtio_console_control msg = {
      .id    = id,
      .event = event,
      .value = value,
  };

  memcpy(ctrl_tx_buf, &msg, sizeof(msg));

  control_tx.desc[desc].addr  = ctrl_tx_phys;
  control_tx.desc[desc].len   = sizeof(msg);
  control_tx.desc[desc].flags = 0;
  control_tx.desc[desc].next  = 0;

  control_tx_busy = 1;
  virtq_submit(&control_tx, desc);
  virtio_queue_notify(&vdev, &control_tx);
  return 0;
}

static int console_setup_clipboard_port(void) {
  if (virtio_queue_setup(&vdev, 4, &clipboard_rx) != 0)
    return -1;

  if (virtio_queue_setup(&vdev, 5, &clipboard_tx) != 0)
    return -1;

  virtio_queue_enable(&vdev, &clipboard_rx);
  virtio_queue_enable(&vdev, &clipboard_tx);

  clipboard_rx_phys = pmm_alloc_frame();

  if (!clipboard_rx_phys)
    return -1;

  clipboard_rx_buf = phys_to_virt(clipboard_rx_phys);
  memset(clipboard_rx_buf, 0, CLIPBOARD_RX_SIZE);

  return 0;
}

static int virtio_console_match(const struct device *dev) {
  if (!dev || dev->bus != DEVICE_BUS_PCI)
    return 0;

  const struct pci_device *pci = &dev->bus_info.pci;

  if (pci->vendor != PCI_VENDOR_VIRTIO)
    return 0;

  log_write_hex("virtio-console: PCI device =", pci->device, KERNEL, LOG_DEBUG);

  return pci->device == PCI_DEVICE_VIRTIO_CONSOLE;
}

static int virtio_console_probe(struct device *dev) {
  if (!dev)
    return -1;

  struct pci_device *pci = &dev->bus_info.pci;

  /* Initialize the existing generic virtio PCI transport. */
  if (virtio_pci_init(pci, &vdev) != 0) {
    log_write("virtio-console: PCI init failed", KERNEL, LOG_ERROR);
    return -1;
  }

  /* Negotiate the multipport feature. */
  if (virtio_negotiate(&vdev, VIRTIO_CONSOLE_F_MULTIPORT) != 0) {
    log_write("virtio-console: feature negotiation failed", KERNEL, LOG_ERROR);
    return -1;
  }

  /* Named ports require MULTIPORT. */
  if (!(vdev.device_features & VIRTIO_CONSOLE_F_MULTIPORT)) {
    log_write("virtio-console: multipport not supported", KERNEL, LOG_ERROR);
    return -1;
  }

  /* Set up port zero's queues. */
  if (virtio_queue_setup(&vdev, 0, &port0_rx) != 0)
    return -1;

  if (virtio_queue_setup(&vdev, 1, &port0_tx) != 0)
    return -1;

  /* Set up the control queues. */
  if (virtio_queue_setup(&vdev, 2, &control_rx) != 0)
    return -1;

  if (virtio_queue_setup(&vdev, 3, &control_tx) != 0)
    return -1;

  virtio_queue_enable(&vdev, &port0_rx);
  virtio_queue_enable(&vdev, &port0_tx);
  virtio_queue_enable(&vdev, &control_rx);
  virtio_queue_enable(&vdev, &control_tx);

  if (console_setup_clipboard_port() != 0) {
    log_write("virtio-console: port setup failed", KERNEL, LOG_ERROR);
    return -1;
  }

  if (console_post_clipboard_rx() != 0) {
    log_write("virtio-console: RX setup failed", KERNEL, LOG_ERROR);
    return -1;
  }

  clipboard_initialized = 1;

  if (console_alloc_control_buffers() != 0)
    return -1;

  if (console_post_control_rx() != 0)
    return -1;

  /*
   * The device can begin processing virtqueue traffic.
   */
  virtio_driver_ok(&vdev);

  vdev.common->queue_select = 4;

  log_write_hex("clipboard: queue size =", vdev.common->queue_size, KERNEL, LOG_INFO);

  log_write_hex("clipboard: queue enabled =", vdev.common->queue_enable, KERNEL, LOG_INFO);

  virtio_queue_notify(&vdev, &clipboard_rx);

  if (console_send_control(0, VIRTIO_CONSOLE_DEVICE_READY, 1) != 0)
    return -1;

  console_initialized = 1;
  dev->driver_data    = &vdev;

  log_write("virtio-console: initialized", KERNEL, LOG_INFO);
  return 0;
}

static int virtio_console_poll(struct device *dev) {
  (void)dev;

  if (!console_initialized)
    return 0;

  int did_work = 0;
  u16 desc;
  u32 len;

  /* Also reap control TX completions so descriptors can be reused. */
  while (virtq_reap(&control_tx, &desc, &len)) {
    virtq_free_desc(&control_tx, desc);
    control_tx_busy = 0;
    did_work        = 1;
  }

  while (virtq_reap(&control_rx, &desc, &len)) {
    did_work = 1;

    if (len >= sizeof(struct virtio_console_control) && len <= CON_CTRL_RX_SIZE) {

      struct virtio_console_control msg;
      memcpy(&msg, ctrl_rx_buf, sizeof(msg));

      log_write_hex("virtio-console: control event =", msg.event, KERNEL, LOG_INFO);
      log_write_hex("virtio-console: port id =", msg.id, KERNEL, LOG_INFO);

      switch (msg.event) {
      case VIRTIO_CONSOLE_PORT_ADD:
        if (msg.id == 1)
          port_ready_pending = 1;
        break;

      case VIRTIO_CONSOLE_PORT_NAME: {
        u32 name_len = len - sizeof(msg);
        char name[128];

        if (name_len >= sizeof(name))
          name_len = sizeof(name) - 1;

        memcpy(name, ctrl_rx_buf + sizeof(msg), name_len);
        name[name_len] = '\0';

        log_write_string("virtio-console: port name =", name, KERNEL, LOG_INFO);
        break;
      }

      case VIRTIO_CONSOLE_PORT_OPEN:
        log_write_hex("virtio-console: PORT_OPEN value =", msg.value, KERNEL, LOG_INFO);
        if (msg.id == 1) {
          clipboard_open_pending = 1;
          clipboard_host_open    = msg.value != 0;
        }
        break;
      default:
        break;
      }
    }

    // Always free and repost, even for invalid control messages.
    virtq_free_desc(&control_rx, desc);

    if (console_post_control_rx() != 0) {
      log_write("virtio-console: control RX repost failed", KERNEL, LOG_ERROR);
      break;
    }
  }

  // IMPORTANT: Outside the control RX loop.
  if (port_ready_pending && !control_tx_busy) {
    if (console_send_control(1, VIRTIO_CONSOLE_PORT_READY, 1) == 0) {
      port_ready_pending = 0;
      did_work           = 1;

      log_write("virtio-console: PORT_READY sent", KERNEL, LOG_INFO);
    }
  }
  if (clipboard_open_pending && !control_tx_busy) {
    if (console_send_control(1, VIRTIO_CONSOLE_PORT_OPEN, 1) == 0) {
      clipboard_open_pending = 0;
      log_write("virtio-console: guest PORT_OPEN sent", KERNEL, LOG_INFO);
    }
  }

  // Clipboard RX polling follows here.
  if (clipboard_initialized) {
    static u16 last_avail = 0xFFFF;
    static u16 last_used  = 0xFFFF;

    u16 avail = clipboard_rx.avail->idx;
    u16 used  = clipboard_rx.used->idx;

    if (avail != last_avail || used != last_used) {
      log_write_hex("clipboard: avail =", avail, KERNEL, LOG_INFO);
      log_write_hex("clipboard: used =", used, KERNEL, LOG_INFO);
      log_write_hex("clipboard: last_used =", clipboard_rx.last_used_idx, KERNEL, LOG_INFO);

      last_avail = avail;
      last_used  = used;
    }

    while (virtq_reap(&clipboard_rx, &desc, &len)) {
      log_write_hex("virtio-console: RX descriptor =", desc, KERNEL, LOG_INFO);

      log_write_hex("virtio-console: RX length =", len, KERNEL, LOG_INFO);

      if (len > 0 && len <= CLIPBOARD_RX_SIZE) {
        char text[128];

        u32 n = len;
        if (n >= sizeof(text))
          n = sizeof(text) - 1;

        memcpy(text, clipboard_rx_buf, n);
        text[n] = '\0';

        log_write_string("virtio-console: RX text =", text, KERNEL, LOG_INFO);
      }

      virtq_free_desc(&clipboard_rx, desc);

      if (console_post_clipboard_rx() != 0)
        break;

      did_work = 1;
    }
  }
  return did_work;
}

static void virtio_console_remove(struct device *dev) {}

const struct driver virtio_console_driver = {.name   = "VirtIO Console",
                                             .bus    = DEVICE_BUS_PCI,
                                             .match  = virtio_console_match,
                                             .probe  = virtio_console_probe,
                                             .poll   = virtio_console_poll,
                                             .remove = NULL};
