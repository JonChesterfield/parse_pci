#include "parse.h"

#include <pci/pci.h>
#include <string.h>

#define HACK 1

#if HACK
struct pci_access *pacc;
__attribute__((constructor)) static void init(void) {
  pacc = pci_alloc();
  pci_init(pacc);
}
__attribute__((destructor)) static void dtor(void) { pci_cleanup(pacc); }
#endif

char *reference(char *namebuf, int size, uint16_t VendorId, uint16_t DeviceId) {
#if !HACK
  struct pci_access *pacc;
  pacc = pci_alloc();
  pci_init(pacc);
#endif
  char *name = pci_lookup_name(pacc, namebuf, size, PCI_LOOKUP_DEVICE, VendorId,
                               DeviceId);

#if !HACK
  pci_cleanup(pacc);
#endif
  return name;
}
