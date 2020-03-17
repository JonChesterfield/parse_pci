#include "EvilUnit.h"
#include "parse.h"

#include <stdbool.h>
#include <string.h>


char *lookup_name(/*optional struct*/ char *buf, int size,
                      /* skip flags */ uint16_t VendorId, uint16_t DeviceID) {
  // Writes into buf, format needs to be consistent
  // Usually returns buf, sometimes returns a pointer into a string literal

  return buf;
  
}

bool consistent(char * ref, char * prop)
{
  if (ref && prop)
    {
      return strcmp(ref, prop) == 0;
    }
  else {
  return false;
  }
}

MAIN_MODULE() {
  char rbuffer[128] = {0};
  char pbuffer[sizeof(rbuffer)] = {0};
  int size = sizeof(rbuffer);

  uint16_t VendorId = 4098;
  uint16_t DeviceId = 26287;
  

  char * ref = reference(rbuffer, size, VendorId, DeviceId);
  char * par = lookup_name(pbuffer, size, VendorId, DeviceId);

  CHECK(consistent(ref, par));
  printf("%s ?= %s\n", ref, par);

}
