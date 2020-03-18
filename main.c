#define _GNU_SOURCE
#include <string.h>

#include "EvilUnit.h"
#include "parse.h"

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>

#include <unistd.h> // why isn't open in this?

#include <sys/mman.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

// Make the tests faster...
static unsigned char *vendor_accel[UINT16_MAX] = {0};

unsigned char *find_vendor(unsigned char *d, size_t n, uint16_t VendorId) {
  {
    unsigned char *a = vendor_accel[VendorId];
    if (a) {
      return a;
    }
  }
  char needle[5];
  sprintf(needle, "\n%04X", VendorId);
  unsigned char *res = memmem(d, n, needle, sizeof(needle));
  if (res) {
    vendor_accel[VendorId] = res;
  }
  return res;
}

unsigned char *find_any_vendor(unsigned char *d, size_t n) {
  // messy
  while (1) {

    unsigned char *nl = memchr(d, '\n', n);
    if (!nl) {
      return NULL;
    }
    size_t moved = nl - d;
    d = nl;
    n -= moved;

    if (n <= 1) {
      return NULL;
    }

    // todo: it's a vendor if the line starts with a (four?) hex digit
    if (isxdigit(d[1])) {
      return d;
    }

    d++;
    n--;
  }
}

unsigned char *find_device(unsigned char *d, size_t n, uint16_t DeviceId) {
  char needle[6] = {0};
  sprintf(needle, "\n\t%04x", DeviceId);
  return memmem(d, n, needle, sizeof(needle));
}

char *lookup_name_from_mmap(unsigned char *d, size_t sz,
                            /*optional struct*/ char *buf, int size,
                            /* skip flags */ const uint16_t VendorId,
                            const uint16_t DeviceId, bool *no_vendor) {

  const bool verbose = false;

  if (verbose) {
    printf("Look up %x/%x\n", VendorId, DeviceId);
  }
  const unsigned char *e = d + sz;
  if (verbose) {
    printf("File size %zu\n", sz);
  }

  unsigned char *found = find_vendor(d, sz, VendorId);
  if (!found) {
    //  If this fails, checking all the device IDs is a poor choice
    *no_vendor = true;

    // reference returns Device here
    snprintf(buf, size - 1, "Device %04x", DeviceId);
    buf[size - 1] = 0;
    return buf;
  }
  assert(found == find_any_vendor(found, sz));

  if (verbose) {
    printf("Scan from %p, found at %p\n", d, found);
  }

  size_t moved = (unsigned char *)found - (unsigned char *)d;
  d = found;
  sz -= moved;

  if (verbose) {
    printf("Found vendor after %zu bytes, leaving %zu\n", moved, sz);
  }

  const unsigned char *next = find_any_vendor(found + 1, sz - 1);
  if (!next) {
    printf("find_any_failed, using end\n");
    next = e;
  }

  if (verbose) {
    printf("looking for device between %p and %p\n", found, next);
    printf("%.*s\n", (int)(10 + next - found), found);
  }

  found = find_device(d, next - found, DeviceId);

  if (!found) {
    snprintf(buf, size - 1, "Device %04x", DeviceId);
    buf[size - 1] = 0;
    return buf;
  }

  // Found = '\n\txxxx -some-whitespace- string - newline
  found += 6; // todo: drop the 6

  while (found != e && isspace(*found)) {
    found++;
  }

  // find next newline or end of file
  unsigned char *newline = found;
  while (newline != e && *newline != '\n') {
    newline++;
  }

  size_t str = newline - found;
  size_t to_copy = (int)str < (size - 1) ? str : (size - 1);

  memcpy(buf, found, to_copy);
  buf[to_copy] = 0;

  return buf;
}

typedef struct {
  bool valid;
  int fd;
  void *addr;
  size_t size;
} mmapped_file_t;

mmapped_file_t open_mmapped_file(void) {

  const char *path = "pci.ids";
  mmapped_file_t res = {
      .valid = false,
  };

  int fd = open(path, O_RDONLY, 0);
  if (fd == -1) {
    return res;
  }

  struct stat sb;
  fstat(fd, &sb);
  size_t sz = sb.st_size;
  void *vd = mmap(0, sz, PROT_READ, MAP_PRIVATE, fd, 0);

  if (vd == MAP_FAILED) {
    close(fd);
    return res;
  }

  res.fd = fd;
  res.addr = vd;
  res.size = sz;
  res.valid = true;
  return res;
}

void close_mmapped_file(mmapped_file_t f) {
  if (f.valid) {
    munmap(f.addr, f.size);
    close(f.fd);
    f.valid = false;
  }
}

char *lookup_name(/*optional struct*/ char *buf, int size,
                  /* skip flags */ uint16_t VendorId, uint16_t DeviceId) {
  // Writes into buf, format needs to be consistent
  // Usually returns buf, sometimes returns a pointer into a string literal

  mmapped_file_t f = open_mmapped_file();

  if (!f.valid) {
    return "<file i/o failed>";
  }

  bool no_vendor;
  char *res = lookup_name_from_mmap(f.addr, f.size, buf, size, VendorId,
                                    DeviceId, &no_vendor);

  close_mmapped_file(f);

  return res;
}

static bool consistent(char *ref, char *prop) {
  if (ref && prop) {
    return strcmp(ref, prop) == 0;
  } else {
    return false;
  }
}

MAIN_MODULE() {

  const bool disable_implementation = false;
  const bool disable_reference = false;

  const bool skip_unknown_vendors = false;

  TEST("") {
    mmapped_file_t file = open_mmapped_file();
    CHECK(file.valid);
    if (file.valid) {
      char rbuffer[128] = {0};
      char pbuffer[sizeof(rbuffer)] = {0};
      int size = sizeof(rbuffer);

      uint16_t VegaDeviceId = 26287;
      uint16_t AMDVendorId = 4098;
      (void)VegaDeviceId;
      (void)AMDVendorId;

      for (uint32_t VendorId = 0; VendorId < UINT16_MAX; VendorId++) {

        if (0) {
          if (VendorId < AMDVendorId - 100)
            continue;
          if (VendorId > AMDVendorId + 100)
            continue;
        }

        printf("Vendor %u\n", VendorId);
        for (uint32_t DeviceId = 0; DeviceId < UINT16_MAX; DeviceId++) {
          if (DeviceId % 100 == 0) {
            // printf("check %u\n", DeviceId);
          }

          // DeviceId = 26672;

          char *ref = disable_reference
                          ? NULL
                          : reference(rbuffer, size, VendorId, DeviceId);

          bool no_vendor = false;
          char *par =
              disable_implementation
                  ? NULL
                  : lookup_name_from_mmap(file.addr, file.size, pbuffer, size,
                                          VendorId, DeviceId, &no_vendor);

          if (disable_implementation || disable_reference) {
            continue;
          }

          if (!consistent(ref, par)) {
            printf("ven/dev %u/%u: ", VendorId, DeviceId);
            printf("%s ?= %s\n", ref, par);
          }

          CHECK(consistent(ref, par));

          if (skip_unknown_vendors && no_vendor) {
            // printf("Can't find vendor %x, skipping devices\n", VendorId);
            break;
          }
        }
      }
    }
    close_mmapped_file(file);
  }
}
