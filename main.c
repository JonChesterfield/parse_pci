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

unsigned char *find_vendor(unsigned char *d, size_t n, uint16_t VendorId) {
  char needle[5];
  sprintf(needle, "\n%04X", VendorId);
  // probably the hot spot
  return memmem(d, n, needle, sizeof(needle));
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

typedef struct {
  unsigned char *start;
  unsigned char *end;
} range_t;

range_t find_vendor_range(unsigned char *d, size_t n, uint16_t VendorId) {
  unsigned char *start = find_vendor(d, n, VendorId);
  if (start) {
    size_t moved = (unsigned char *)start - (unsigned char *)d;

    // search from just past vendor initially looked for
    unsigned char *end = find_any_vendor(start + 1, n - moved - 1);
    if (!end) {
      end = d + n;
    }

    return (range_t){.start = start, .end = end};
  }

  return (range_t){.start = NULL};
}

unsigned char *find_device(unsigned char *d, size_t n, uint16_t DeviceId) {
  char needle[6] = {0};
  sprintf(needle, "\n\t%04x", DeviceId);
  return memmem(d, n, needle, sizeof(needle));
}

char *find_device_within_range(range_t vendor, char *buf, int size,
                               const uint16_t DeviceId) {
  assert(vendor.start);
  unsigned char *dev =
      find_device(vendor.start, vendor.end - vendor.start, DeviceId);

  if (!dev) {
    snprintf(buf, size - 1, "Device %04x", DeviceId);
    buf[size - 1] = 0;
    return buf;
  }

  // Found = '\n\txxxx -some-whitespace- string - newline
  dev += 6; // todo: drop the 6

  // Trim leading whitespace
  while (dev != vendor.end && isspace(*dev)) {
    dev++;
  }

  // find next newline or end of file
  unsigned char *newline = dev;
  while (newline != vendor.end && *newline != '\n') {
    newline++;
  }

  size_t str = newline - dev;
  size_t to_copy = (int)str < (size - 1) ? str : (size - 1);

  memcpy(buf, dev, to_copy);
  buf[to_copy] = 0;

  return buf;
}

char *lookup_name_from_mmap(unsigned char *d, size_t sz,
                            /*optional struct*/ char *buf, int size,
                            /* skip flags */ const uint16_t VendorId,
                            const uint16_t DeviceId, bool *no_vendor) {

  const bool verbose = false;

  if (verbose) {
    printf("Look up %x/%x\n", VendorId, DeviceId);
  }
  if (verbose) {
    printf("File size %zu\n", sz);
  }

  range_t vendor = find_vendor_range(d, sz, VendorId);
  if (!vendor.start) {
    //  If this fails, checking all the device IDs is a poor choice
    *no_vendor = true;

    // reference returns Device here
    snprintf(buf, size - 1, "Device %04x", DeviceId);
    buf[size - 1] = 0;
    return buf;
  }

  return find_device_within_range(vendor, buf, size, DeviceId);
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

  TEST("linear") {
    return;
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

  TEST("faster") {
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

        range_t vendor = find_vendor_range(file.addr, file.size, VendorId);

        if (!vendor.start) {
          // Reference will return "Device xxxx", can check out of line
          continue;
        }

        printf("Check vendor %u (%x)\n", VendorId, VendorId);

        for (uint32_t DeviceId = 0; DeviceId < UINT16_MAX; DeviceId++) {

          char *ref = reference(rbuffer, size, VendorId, DeviceId);
          char *par = find_device_within_range(vendor, pbuffer, size, DeviceId);

          if (!consistent(ref, par)) {
            printf("ven/dev %u/%u: ", VendorId, DeviceId);
            printf("%s ?= %s\n", ref, par);
          }

          CHECK(consistent(ref, par));
        }
      }
    }
    close_mmapped_file(file);
  }
}
