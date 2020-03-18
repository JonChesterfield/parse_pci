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

/*
 * Parse the pci.ids file
 * Syntax:
 * '#' starts a comment line
 * # vendor  vendor_name
 * #	device  device_name				<-- single tab
 * #		subvendor subdevice  subsystem_name	<-- two tabs
 * This parser seeks 'device_name' and ignores subvendor fields
 */

struct range {
  // Iterator pair, start <= end. Can dereference [start end)
  unsigned char *start;
  unsigned char *end;
};
static bool empty_range(struct range r) { return r.start == r.end; }

static void write_as_four_hex_chars(uint16_t x, char *b) {
  const char digits[] = "0123456789abcdef";
  for (unsigned i = 0; i < 4; i++) {
    unsigned index = 0xf & (x >> 4 * (3 - i));
    b[i] = digits[index];
  }
}

static struct range find_vendor(struct range r, uint16_t VendorId)

{
  if (empty_range(r)) {
    return r;
  }

  char needle[5] = "\n0000";
  write_as_four_hex_chars(VendorId, &needle[1]);
  unsigned char *s = memmem(r.start, r.end - r.start, needle, sizeof(needle));

  if (s) {
    r.start = s;
  } else {
    r.start = r.end;
    assert(empty_range(r));
  }
  return r;
}

/*
 *
 * 1002  Advanced Micro Devices, Inc. [AMD/ATI]
 *	1304  Kaveri
 *	1305  Kaveri
 * # comments...
 *	cbb2  RS200 Host Bridge
 * 1003  ULSI Systems
 *	0201  US201
 * 1004  VLSI Technology Inc
 * 	0005  82C592-FC1
 * 	0006  82C593-FC1
 *
 */

static struct range find_device(struct range r, uint16_t DeviceId) {
  if (empty_range(r)) {
    return r;
  }
  assert(r.start[0] == '\n');

  // Start of region is the vendor ID. Skip over it.
  r.start++;
  if (empty_range(r)) {
    goto fail;
  }
  r.start = memchr(r.start, '\n', r.end - r.start);
  if (!r.start) {
    goto fail;
  }

  assert(r.start[0] == '\n');

  char needle[6] = "\n\t0000";
  write_as_four_hex_chars(DeviceId, &needle[2]);

  for (;;) {
    size_t width = r.end - r.start;

    if (width < 6) {
      goto fail;
    }

    unsigned char *end = memchr(r.start + 1, '\n', width - 1);
    if (!end) {
      end = r.end;
    }

    if (memcmp(r.start, needle, sizeof(needle)) == 0) {
      // matched. Narrow the range to the printed result
      r.start += 6;
      r.end = end;
      // trim leading and trailing whitespace
      while (!empty_range(r) && isspace(r.start[0])) {
        r.start++;
      }
      while (!empty_range(r) && isspace(r.end[-1])) {
        r.end--;
      }

      // trim whitespace from both ends
      return r;
    }

    if (isxdigit(r.start[1])) {
      // Reached the end of this region
      goto fail;
    }

    // Otherwise ignore whatever is on the line, e.g. '#'

    r.start = end;
  }

fail:
  return (struct range){0, 0};
}

static void copy_range_to_buffer(struct range r, char *buf, int size) {
  assert(!empty_range(r));
  int to_copy = (int)(r.end - r.start);
  to_copy = to_copy < (size - 1) ? to_copy : size - 1;

  memcpy(buf, r.start, to_copy);
  buf[to_copy] = '\0';
}

__attribute__((used)) static void write_fallback_to_buffer(char *buf, int size,
                                                           uint16_t DeviceId) {
  char tmp[] = "Device xxxx";
  static_assert(sizeof(tmp) == 12, "");
  write_as_four_hex_chars(DeviceId, &tmp[7]);

  int to_copy = ((int)sizeof(tmp) <= size) ? sizeof(tmp) : size;
  memcpy(buf, tmp, to_copy);
  buf[size - 1] = '\0';
}

static void fill_buffer_from_device_range(char *buf, int size, struct range r,
                                          uint16_t DeviceId) {
  if (empty_range(r)) {
    write_fallback_to_buffer(buf, size, DeviceId);

  } else {
    copy_range_to_buffer(r, buf, size);
  }
}

typedef struct {
  int fd;
  unsigned size;
  void *addr;
} mmapped_file_t;

static_assert(sizeof(mmapped_file_t) == 16, "");
static mmapped_file_t open_mmapped_file(void) {

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

static void close_mmapped_file(mmapped_file_t f) {
  if (f.fd != -1) {
    munmap(f.addr, f.size);
    close(f.fd);
  }
}

char *lookup_name(char *buf, int size, uint16_t VendorId, uint16_t DeviceId) {
  // Writes into buf, format needs to be consistent
  // Usually returns buf, sometimes returns a pointer into a string literal

  mmapped_file_t f = open_mmapped_file();

  if (f.fd == -1) {
    write_fallback_to_buffer(buf, size, DeviceId);
    return buf;
  }

  struct range whole_file = {
      .start = f.addr,
      .end = f.addr + f.size,
  };

  struct range vendor = find_vendor(whole_file, VendorId);

  struct range device = find_device(vendor, DeviceId);

  if (!empty_range(device)) {
    copy_range_to_buffer(device, buf, size);
  } else {
    write_fallback_to_buffer(buf, size, DeviceId);
  }

  close_mmapped_file(f);
  return buf;
}

static bool consistent(char *ref, char *prop) {
  if (ref && prop) {
    return strcmp(ref, prop) == 0;
  } else {
    return false;
  }
}

static MODULE(write_as_four_hex) {
  TEST("vs sprintf") {
    char ref[5];
    char val[4];
    for (uint32_t i = 0; i < UINT16_MAX; i++) {
      write_as_four_hex_chars(i, val);
      CHECK(4 == snprintf(ref, 5, "%04x", i));
      CHECK(memcmp(ref, val, 4) == 0);
    }
  }
}

MAIN_MODULE() {
  DEPENDS(write_as_four_hex);

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

        struct range vendor = find_vendor(
            (struct range){.start = file.addr, file.addr + file.size},
            VendorId);

        if (empty_range(vendor)) {
          // Reference will return "Device xxxx", can check out of line
          continue;
        }

        printf("Check vendor %u (0x%04x)\n", VendorId, VendorId);

        for (uint32_t DeviceId = 0; DeviceId < UINT16_MAX; DeviceId++) {

          char *ref = reference(rbuffer, size, VendorId, DeviceId);

          struct range device = find_device(vendor, DeviceId);

          fill_buffer_from_device_range(pbuffer, size, device, DeviceId);
          char *par = pbuffer;

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
