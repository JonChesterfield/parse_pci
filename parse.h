#ifndef PARSE_H
#define PARSE_H

#include <stdint.h>

char * reference(char * namebuf , int size, uint16_t VendorId, uint16_t DeviceID);

char *lookup_name(/*optional struct*/ char *buf, int size,
                  /* skip flags */ uint16_t VendorId, uint16_t DeviceID) ;

#endif
