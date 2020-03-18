#include "EvilUnit.h"
#include "pci_ids.h"

#include <stdbool.h>
#include <string.h>

#include <pci/pci.h>
#include <string.h>

#define GLOBAL_CTOR 1

#if GLOBAL_CTOR
struct pci_access *pacc;
__attribute__((constructor)) static void init(void) {
  pacc = pci_alloc();
  pci_init(pacc);
}
__attribute__((destructor)) static void dtor(void) { pci_cleanup(pacc); }
#endif

char *reference(char *namebuf, int size, uint16_t VendorId, uint16_t DeviceId) {
#if !GLOBAL_CTOR
  struct pci_access *pacc;
  pacc = pci_alloc();
  pci_init(pacc);
#endif
  char *name = pci_lookup_name(pacc, namebuf, size, PCI_LOOKUP_DEVICE, VendorId,
                               DeviceId);

#if !GLOBAL_CTOR
  pci_cleanup(pacc);
#endif
  return name;
}



static bool consistent(char *ref, char *prop) {
  if (ref && prop) {
    return strcmp(ref, prop) == 0;
  } else {
    return false;
  }
}


// Fix this - cache should work with oversized buffer
#ifndef HSA_PUBLIC_NAME_SIZE
#define HSA_PUBLIC_NAME_SIZE 64
#endif

MAIN_MODULE() {

  TEST("external") {
    struct pci_ids file = pci_ids_create();
    CHECK(file.fd != -1);
    if (file.fd != -1) {
      char rbuffer[HSA_PUBLIC_NAME_SIZE] = {0};
      char pbuffer[sizeof(rbuffer)] = {0};
      size_t size = sizeof(rbuffer);

      uint16_t VegaDeviceId = 26287;
      uint16_t AMDVendorId = 4098;
      (void)VegaDeviceId;
      (void)AMDVendorId;

      for (uint32_t VendorId = 0; VendorId <= UINT16_MAX; VendorId++) {

        printf("Check vendor %u (0x%04x)\n", VendorId, VendorId);

        for (uint32_t DeviceId = 0; DeviceId <= UINT16_MAX; DeviceId++) {

          char *ref = reference(rbuffer, size, VendorId, DeviceId);
          char *par = pci_ids_lookup(file, pbuffer, size, VendorId, DeviceId);

          if (!consistent(ref, par)) {
            printf("ven/dev %u/%u: ", VendorId, DeviceId);
            printf("%s ?= %s\n", ref, par);
          }

          CHECK(consistent(ref, par));
        }
      }
    }
    pci_ids_destroy(file);
  }
}
