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

  return (range_t){0, 0};
}

range_t find_vendor_2(range_t r, uint16_t VendorId)

{
  char needle[5];
  sprintf(needle, "\n%04X", VendorId);
  unsigned char *s = memmem(r.start, r.end - r.start, needle, sizeof(needle));
  if (!s) {
    r.start = r.end;
  } else {
    r.start = s;
  }
  return r;
}

range_t find_device_2(range_t r, uint16_t DeviceId) {
  assert(r.start != r.end);
  assert(r.start[0] == '\n');

  char needle[7] = {0};
  int rc = sprintf(needle, "\n\t%04x", DeviceId);
  assert(rc >= 0); // todo: check

  for (unsigned idx = 0;; idx++) {
    size_t width = r.end - r.start;

    if (width < 6) {
      goto fail;
    }

    unsigned char *end = memchr(r.start + 1, '\n', width - 1);
    if (!end) {
      end = r.end;
    }

    if (memcmp(r.start, needle, 6) == 0) {
      // matched. Narrow the range to the printed result
      r.start += 6;
      r.end = end;
      // trim leading whitespace
      while (r.start != r.end && isspace(*r.start)) {
        r.start++;
      }

      if (r.start != r.end) {
        // TODO: test this logic
        unsigned char *c = r.end;
        c--;
        while (r.start != c && isspace(*c)) {
          c--;
        }
        r.end = c + 1;
      }

      // need to trim the tail too

      // trim whitespace from both ends
      return r;
    }

    if (isxdigit(r.start[0])) {
      // Reached the end of this region
      printf("Fail at %d\n", __LINE__);
      goto fail;
    }

    // Otherwise ignore whatever is on the line, e.g. '#'

    r.start = end;
  }

fail:
  return (range_t){0, 0};
}

unsigned char *find_device(unsigned char *d, size_t n, uint16_t DeviceId) {
  // This can handle comments etc
  char needle[6] = {0};
  sprintf(needle, "\n\t%04x", DeviceId);
  return memmem(d, n, needle, sizeof(needle));
}

char *find_device_within_range(range_t vendor, char *buf, int size,
                               const uint16_t DeviceId) {
  assert(vendor.start);

  bool novel = true;

  if (novel) {

    range_t dev_r = find_device_2(vendor, DeviceId);
    if (dev_r.start == dev_r.end) {
      snprintf(buf, size - 1, "Device %04x", DeviceId);
      buf[size - 1] = 0;
      return buf;
    } else {
      // todo: width
      int to_copy = (int)(dev_r.end - dev_r.start);
      to_copy = to_copy < (size - 1) ? to_copy : size - 1;
      memcpy(buf, dev_r.start, to_copy);
      buf[to_copy] = 0;
      return buf;
    }
  }

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

  const bool verbose = true;

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
  int fd;
  unsigned size;
  void *addr;
} mmapped_file_t;

static_assert(sizeof(mmapped_file_t) == 16, "");
mmapped_file_t open_mmapped_file(void) {

  const char *path = "pci.ids";
  mmapped_file_t failure = {
      .fd = -1,
  };

  int fd = open(path, O_RDONLY, 0);
  if (fd == -1) {
    return failure;
  }

  struct stat sb;
  fstat(fd, &sb);
  size_t sz = sb.st_size;

  if (sz == 0) {
    close(fd);
    return failure;
  }
  // No attempt to read files > 4gb in size
  sz = (sz < UINT32_MAX) ? sz : UINT32_MAX;
  void *addr = mmap(0, sz, PROT_READ, MAP_PRIVATE, fd, 0);

  if (addr == MAP_FAILED) {
    close(fd);
    return failure;
  }

  return (mmapped_file_t){
      .fd = fd,
      .addr = addr,
      .size = sz,
  };
}

void close_mmapped_file(mmapped_file_t f) {
  if (f.fd != -1) {
    munmap(f.addr, f.size);
    close(f.fd);
  }
}

char *lookup_name(char *buf, int size, uint16_t VendorId, uint16_t DeviceId) {
  // Writes into buf, format needs to be consistent
  // Usually returns buf, sometimes returns a pointer into a string literal

  mmapped_file_t f = open_mmapped_file();

  if (f.fd != -1) {
    range_t whole_file = {
        .start = f.addr,
        .end = f.addr + f.size,
    };
    (void)whole_file;
  }
  // Wrong failure mode
  if (f.fd != -1) {
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

  TEST("") {
    mmapped_file_t file = open_mmapped_file();
    CHECK(file.fd != -1);
    if (file.fd != -1) {
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
