#include "pci_ids.c" // <- not the header

// Shipping to a repo which doesn't do fine grained testing, but want to test the
// implementation details
#include "EvilUnit.h"
#include <stdlib.h>

bool equal_range(struct range x, struct range y)
{
	size_t xsz = x.end - x.start;
	size_t ysz = y.end - y.start;
	if (xsz != ysz) {
		return false;
	}

	return memcmp(x.start, y.start, xsz) == 0;
}

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

struct range make_range(const char *str)
{
	assert(str);
	size_t len = strlen(str);
	unsigned char *mem = malloc(len);
	assert(mem);
	memcpy(mem, str, len); // excludes the trailing nul
	return (struct range){
		.start = mem,
		.end = mem + len,
	};
}

static MODULE(trim)
{
	TEST("")
	{
		CHECK(equal_range(trim_whitespace(make_range("")),
				  trim_whitespace(make_range(" \n \t "))));

		CHECK(equal_range(trim_whitespace(make_range("leading")),
				  trim_whitespace(make_range("\vleading"))));

		CHECK(equal_range(trim_whitespace(make_range("trailing")),
				  trim_whitespace(make_range("trailing\n"))));

		CHECK(equal_range(trim_whitespace(make_range("both")),
				  trim_whitespace(make_range(" \n\tboth  \r"))));
	}
}

MAIN_MODULE()
{
	DEPENDS(write_as_hex);
	DEPENDS(trim);
	CHECK(equal_range(make_range(""), make_range("")));

	// DEPENDS(regression);
}
