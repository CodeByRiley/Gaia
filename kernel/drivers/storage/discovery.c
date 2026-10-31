#include <drivers/storage/ahci.h>
#include <drivers/storage/ahci_block.h>
#include <drivers/storage/blockdev.h>
#include <drivers/storage/discovery.h>
#include <drivers/storage/partition.h>
#include <drivers/usb/storage/usb_storage.h>

#include <utilities/log.h>
#include <utilities/string.h>

static struct ahci_block_device ahci_disks[AHCI_MAX_PORTS];

int storage_publish_ahci_port(struct AHCI_DEVICE_DATA *controller, int port) {
  if (!controller || port < 0 || port >= AHCI_MAX_PORTS)
    return -1;

  struct ahci_block_device *disk = &ahci_disks[port];

  // Already published.
  if (disk->device.context)
    return 0;

  // This initializes the block adapter and identifies disk capacity.
  if (ahci_block_open(disk, controller, port) != 0)
    return -1;

  char name[BLOCKDEV_NAME_MAX];

  // Keep Gaia's existing port-based naming.
  snprintf(name, sizeof(name), "ahci%d", port);

  if (blockdev_register(name, &disk->device, 0, 0) != 0) {
    ahci_block_close(disk);
    return -1;
  }

  if (partition_scan(&disk->device, name, 0) > 0)
    blockdev_set_flags(name, BLOCKDEV_PARTITIONED);

  return 0;
}

int storage_publish_usb(size_t index) {
  const struct block_device *disk = usb_storage_device(index);

  if (!disk)
    return -1;

  char name[BLOCKDEV_NAME_MAX];
  snprintf(name, sizeof(name), "usb%u", (unsigned)index);

  struct block_device existing;

  // Already registered, don't publish twice.
  if (blockdev_lookup(name, &existing) == 0)
    return 0;

  int result = blockdev_register(name, disk, 0, BLOCKDEV_REMOVABLE);
  if (result != 0)
    return result;

  if (partition_scan(disk, name, BLOCKDEV_REMOVABLE) > 0)
    blockdev_set_flags(name, BLOCKDEV_PARTITIONED);

  return 0;
}
