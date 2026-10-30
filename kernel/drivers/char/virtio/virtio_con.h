#ifndef VIRTIO_CON_H
#define VIRTIO_CON_H

#include <stdint.h>

/* Device feature bits. */
#define VIRTIO_CONSOLE_F_SIZE      (1ULL << 0)
#define VIRTIO_CONSOLE_F_MULTIPORT (1ULL << 1)
#define VIRTIO_CONSOLE_F_EMERG_WRITE (1ULL << 2)

/* Control message events. */
#define VIRTIO_CONSOLE_DEVICE_READY 0
#define VIRTIO_CONSOLE_PORT_ADD     1
#define VIRTIO_CONSOLE_PORT_REMOVE  2
#define VIRTIO_CONSOLE_PORT_READY   3
#define VIRTIO_CONSOLE_CONSOLE_PORT 4
#define VIRTIO_CONSOLE_RESIZE       5
#define VIRTIO_CONSOLE_PORT_OPEN    6
#define VIRTIO_CONSOLE_PORT_NAME    7

/* Control message header. */
struct virtio_console_control {
  uint32_t id;
  uint16_t event;
  uint16_t value;
} __attribute__((packed));

extern const struct driver virtio_console_driver;

#endif
