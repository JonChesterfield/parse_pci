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

void dump_range(struct range r)
{
	int size = r.end - r.start;
	char *tmp = malloc(size + 1);
	assert(tmp);
	memcpy(tmp, r.start, size);
	tmp[size] = '\0';
	printf("%d:%.*s\n", size, size, tmp);
	free(tmp);
}

#define CHECKR(X, Y)                                                           \
	{                                                                      \
		struct range a = X;                                            \
		struct range b = Y;                                            \
		CHECK(equal_range(a, b));                                      \
		if (!equal_range(a, b)) {                                      \
			dump_range(a);                                         \
			dump_range(b);                                         \
		}                                                              \
	}

static MODULE(util)
{
	const struct range fail = make_range("");

	TEST("trim")
	{
		CHECKR(make_range(""), trim_whitespace(make_range(" \n \t ")));

		CHECKR(make_range("leading"),
		       trim_whitespace(make_range("\vleading")));

		CHECKR(make_range("trailing"),
		       trim_whitespace(make_range("trailing\n")));

		CHECKR(make_range("both"),
		       trim_whitespace(make_range(" \n\tboth  \r")));
	}

	TEST("skip_vendor_id")
	{
		CHECKR(fail, skip_vendor_id(make_range("\n")));

		CHECKR(fail, skip_vendor_id(make_range("\nA")));

		CHECKR(fail, skip_vendor_id(make_range("\n\tn")));

		CHECKR(make_range("\n"), skip_vendor_id(make_range("\n\n")));

		CHECKR(make_range("\n\t1234 stuff"),
		       skip_vendor_id(make_range("\nabcd Extra things\n"
						 "\t1234 stuff")));
	}
}

MAIN_MODULE()
{
	DEPENDS(write_as_hex);
	DEPENDS(util);
	CHECK(equal_range(make_range(""), make_range("")));

	// DEPENDS(regression);
}
