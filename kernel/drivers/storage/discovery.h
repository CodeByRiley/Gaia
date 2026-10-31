#ifndef STORAGE_DISCOVERY_H
#define STORAGE_DISCOVERY_H

#include <stddef.h>
struct AHCI_DEVICE_DATA;

int storage_publish_ahci_port(struct AHCI_DEVICE_DATA *controller, int port);
int storage_publish_usb(size_t index);

#endif
