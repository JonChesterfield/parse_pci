#include "pci_ids.c" // <- not the header

// Shipping to a repo which doesn't do fine grained testing, but want to test the
// implementation details
#include "EvilUnit.h"

static MODULE(write_as_hex)
{
	TEST("vs sprintf")
	{
		char ref[5];
		char val[4];
		for (uint32_t i = 0; i < UINT16_MAX; i++) {
			write_as_hex(i, val);
			CHECK(4 == snprintf(ref, 5, "%04x", i));
			CHECK(memcmp(ref, val, 4) == 0);
		}
	}
}

MAIN_MODULE()
{
	DEPENDS(write_as_hex);
	DEPENDS(regression);
}
