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

int open_file() {
  const char *path = "pci.ids";
  return open(path, O_RDONLY, 0);
}

unsigned char *find_vendor(unsigned char *d, size_t n, uint16_t VendorId) {
  char needle[5];
  sprintf(needle, "\n%04X", VendorId);
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
    if (d[1] != '\t') {
      return d;
    }

    d++;
    n--;
  }
}

unsigned char *find_device(unsigned char *d, size_t n, uint16_t DeviceId) {
  char needle[6];
  sprintf(needle, "\n\t%04x", DeviceId);
  return memmem(d, n, needle, sizeof(needle));
}

char *lookup_name_from_mmap(unsigned char *d, size_t sz,
                            /*optional struct*/ char *buf, int size,
                            /* skip flags */ uint16_t VendorId,
                            uint16_t DeviceId) {

  const bool verbose = false;

  const unsigned char *e = d + sz;
  if (verbose) {
    printf("File size %zu\n", sz);
  }

  unsigned char *found = find_vendor(d, sz, VendorId);
  if (!found) {
    return "<can't find vendor id>";
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
    next = e;
  }

  found = find_device(d, next - found, DeviceId);
  if (!found) {
    snprintf(buf, size - 1, "Device %04x", DeviceId);
    buf[size - 1] = 0;
    return buf;
  }

  // Found = '\n\txxxx -some-whitespace- string - newline
  found += 6;

  while (found != e && isspace(*found)) {
    found++;
  }

  // find next newline or end of file
  unsigned char *newline = found;
  while (newline != e && *newline != '\n') {
    newline++;
  }

  size_t str = newline - found;
  size_t to_copy = str < (size - 1) ? str : (size - 1);

  // hazard, don't copy beyond the file
  memcpy(buf, found, to_copy);
  buf[to_copy] = 0;

  return buf;
}

char *lookup_name(/*optional struct*/ char *buf, int size,
                  /* skip flags */ uint16_t VendorId, uint16_t DeviceId) {
  // Writes into buf, format needs to be consistent
  // Usually returns buf, sometimes returns a pointer into a string literal

  int fd = open_file();
  if (fd == -1) {
    return "<can't open it>";
  }
  struct stat sb;
  fstat(fd, &sb);
  size_t sz = sb.st_size;
  void *vd = mmap(0, sz, PROT_READ, MAP_PRIVATE, fd, 0);

  if (vd == MAP_FAILED) {
    return "<can't map it>";
  }

  unsigned char *d = vd;

  char *res = lookup_name_from_mmap(d, sz, buf, size, VendorId, DeviceId);

  munmap(vd, sz);
  close(fd);

  return res;
}

bool consistent(char *ref, char *prop) {
  if (ref && prop) {
    return strcmp(ref, prop) == 0;
  } else {
    return false;
  }
}

MAIN_MODULE() {

  TEST("") {
    char rbuffer[128] = {0};
    char pbuffer[sizeof(rbuffer)] = {0};
    int size = sizeof(rbuffer);

    uint16_t VegaDeviceId = 26287;

    uint16_t VendorId = 4098;
    uint64_t off = 50;
    for (uint16_t DeviceId = VegaDeviceId - off; DeviceId < VegaDeviceId + off;
         DeviceId++) {

      char *ref = reference(rbuffer, size, VendorId, DeviceId);
      char *par = lookup_name(pbuffer, size, VendorId, DeviceId);

      if (!consistent(ref, par)) {
        printf("%s != %s\n", ref, par);
      }
      CHECK(consistent(ref, par));
    }
  }
}
