/*
 * Copyright © 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including
 * the next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Parse a pci.ids text file to extract 'device_name'
 * # Vendors, devices and subsystems. Please keep sorted.
 * # Syntax:
 * # vendor  vendor_name
 * #	device  device_name				<-- single tab
 * #		subvendor subdevice  subsystem_name	<-- two tabs
 */

/*
 * Example section of file. Searching for 1002/130c
 *
 * 1002  Advanced Micro Devices, Inc. [AMD/ATI]
 * # fields elided
 *	130a  Kaveri [Radeon R6 Graphics]
 *	130b  Kaveri [Radeon R4 Graphics]
 *
 *	130c  Kaveri [Radeon R7 Graphics] <- result
 * #          ^-------------------------^
 *
 *	130d  Kaveri [Radeon R6 Graphics]
 *	130e  Kaveri [Radeon R5 Graphics]
 * # fields elided
 * # next vendor region starts
 * 1003  ULSI Systems
 *	0201  US201
 */

#define _GNU_SOURCE
#include <string.h>

#include "pci_ids.h"

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdio.h>
// #include "EvilUnit.h"
// #include "parse.h"

static const char *pci_ids_paths[] = {
    "/usr/share/hwdata/pci.ids", // update-pciids
    "/usr/share/misc/pci.ids",   // debian
    "/usr/share/pci.ids",        // redhat
    "/var/lib/pciutils/pci.ids", // also debian
    "pci.ids",
};

static struct pci_ids pci_ids_create_from_file(const char *path) {
  struct pci_ids failure = {
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

  sz = (sz < UINT32_MAX) ? sz : UINT32_MAX;
  void *addr = mmap(0, sz, PROT_READ, MAP_PRIVATE, fd, 0);

  if (addr == MAP_FAILED) {
    close(fd);
    return failure;
  }

  return (struct pci_ids){
      .fd = fd,
      .addr = addr,
      .size = sz,
  };
}

struct pci_ids pci_ids_create(void) {
  size_t sz = sizeof(pci_ids_paths) / sizeof(pci_ids_paths[0]);
  for (size_t i = 0; i < sz; i++) {
    struct pci_ids res = pci_ids_create_from_file(pci_ids_paths[i]);
    if (res.fd != -1) {
      return res;
    }
  }

  return (struct pci_ids){.fd = -1};
}

void pci_ids_destroy(struct pci_ids f) {
  if (f.fd != -1) {
    munmap(f.addr, f.size);
    close(f.fd);
  }
}

struct range {
  // Iterator pair, start <= end. Can dereference [start end)
  unsigned char *start;
  unsigned char *end;
};
static bool empty_range(struct range r) { return r.start == r.end; }

static void write_as_hex(uint16_t x, char *b) {
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
  write_as_hex(VendorId, &needle[1]);
  unsigned char *s = memmem(r.start, r.end - r.start, needle, sizeof(needle));

  if (s) {
    r.start = s;
  } else {
    r.start = r.end;
    assert(empty_range(r));
  }
  return r;
}

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
  write_as_hex(DeviceId, &needle[2]);

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

static void copy_range_to_buffer(struct range r, char *buf, size_t size) {
  assert(!empty_range(r));
  size_t to_copy = (r.end - r.start);
  to_copy = to_copy < (size - 1) ? to_copy : size - 1;

  memcpy(buf, r.start, to_copy);
  buf[to_copy] = '\0';
}

__attribute__((used)) static void
write_fallback_to_buffer(char *buf, size_t size, uint16_t DeviceId) {
  char tmp[] = "Device xxxx";
  static_assert(sizeof(tmp) == 12, "");
  write_as_hex(DeviceId, &tmp[7]);

  size_t to_copy = (sizeof(tmp) <= size) ? sizeof(tmp) : size;
  memcpy(buf, tmp, to_copy);
  buf[size - 1] = '\0';
}

#define CACHE 0

#if CACHE

#ifndef HSA_PUBLIC_NAME_SIZE
#define HSA_PUBLIC_NAME_SIZE 64
#endif

char *pci_ids_lookup(struct pci_ids f, char *buf, size_t size,
                     uint16_t VendorId, uint16_t DeviceId) {

  // Probably want a mutex, but don't want to hold it for the whole routine
  static struct cache {
    uint16_t VendorId;
    uint16_t DeviceId;
    uint32_t VendorOffset;          // ~0 if vendor not found
    char res[HSA_PUBLIC_NAME_SIZE]; // res[0] == '\0' if unavailable
  } instance = {UINT16_MAX, UINT16_MAX, UINT32_MAX, {0}};

  if (f.fd == -1) {
    write_fallback_to_buffer(buf, size, DeviceId);
    return buf;
  }

  if (instance.VendorId == VendorId) {
    if (instance.VendorOffset == UINT32_MAX) {
      write_fallback_to_buffer(buf, size, DeviceId);
      return buf;
    }
    if (instance.DeviceId == DeviceId && instance.res[0] != '\0') {
      size_t len = strlen(instance.res);
      if (len < size) {
        memcpy(buf, instance.res, len + 1);
        assert(buf[len] == '\0');
        return buf;
      }
    }
  }

  struct range whole_file = {
      .start = f.addr,
      .end = f.addr + f.size,
  };

  struct range vendor = {0, 0};

  if (instance.VendorId == VendorId) {
    // Attempt to populate the vendor location from cache
    uint32_t off = instance.VendorOffset;
    char needle[5] = "\n0000";
    if ((off + sizeof(needle)) < f.size) {
      unsigned char *guess = f.addr + off;
      write_as_hex(VendorId, &needle[1]);
      if (memcmp(guess, needle, sizeof(needle)) == 0) {
        vendor.start = guess;
        vendor.end = whole_file.end;
      }
    }
  }

  if (empty_range(vendor)) {
    vendor = find_vendor(whole_file, VendorId);
  }

  struct range device = find_device(vendor, DeviceId);

  if (!empty_range(device)) {
    copy_range_to_buffer(device, buf, size);
  } else {
    write_fallback_to_buffer(buf, size, DeviceId);
  }

  instance.VendorId = VendorId;
  instance.DeviceId = DeviceId;
  instance.VendorOffset =
      empty_range(vendor) ? UINT32_MAX : vendor.start - whole_file.start;

  size_t used = strlen(buf) + 1;
  if (used < sizeof(instance.res)) {
    memcpy(instance.res, buf, used);
    assert(instance.res[used - 1] == '\0');
  } else {
    instance.res[0] = '\0';
  }

  return buf;
}

#else

char *pci_ids_lookup(struct pci_ids f, char *buf, size_t size,
                     uint16_t VendorId, uint16_t DeviceId) {

  if (f.fd == -1) {
    write_fallback_to_buffer(buf, size, DeviceId);
    return buf;
  }

  struct range whole_file = {
      .start = f.addr,
      .end = (unsigned char*)f.addr + f.size,
  };

  struct range vendor = find_vendor(whole_file, VendorId);

  struct range device = find_device(vendor, DeviceId);

  if (!empty_range(device)) {
    copy_range_to_buffer(device, buf, size);
  } else {
    write_fallback_to_buffer(buf, size, DeviceId);
  }

  return buf;
}

#endif
