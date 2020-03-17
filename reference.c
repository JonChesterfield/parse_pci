#include "parse.h"


#include <pci/pci.h>
#include <string.h>

char *reference(char * namebuf , int size, uint16_t VendorId,
                uint16_t DeviceId) {
  struct pci_access *pacc;
  pacc = pci_alloc();
  pci_init(pacc);

  char *name = pci_lookup_name(pacc, namebuf, size, PCI_LOOKUP_DEVICE, VendorId,
                               DeviceId);

  pci_cleanup(pacc);
  return name;
}
