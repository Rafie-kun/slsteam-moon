// MemHlp::rangeContains must never accept a wrapped address.
#include "memhlp.hpp"

#include <cstdint>
#include <cstdio>

static int failures = 0;
static void expect(bool ok, const char* text)
{
	std::printf("%s: %s\n", ok ? "ok" : "FAIL", text);
	if (!ok)
		++failures;
}

int main()
{
	using MemHlp::rangeContains;
	const lm_address_t b = 0x7f0000001000ull;
	const lm_address_t e = 0x7f0000003000ull;
	expect(rangeContains(b, e, b, 8), "first word");
	expect(rangeContains(b, e, e - 8, 8), "last word");
	expect(!rangeContains(b, e, e - 7, 8), "word crossing the end");
	expect(!rangeContains(b, e, b - 1, 8), "word starting before the range");
	expect(!rangeContains(b, e, e, 1), "address at the exclusive end");
	expect(!rangeContains(b, e, 0, 8), "null");
	expect(!rangeContains(b, e, static_cast<lm_address_t>(0) - 8, 8), "null minus a word wraps to 0xff..f8");
	expect(!rangeContains(b, e, ~static_cast<lm_address_t>(0), 8), "all ones");
	expect(!rangeContains(b, e, b, e - b + 1), "size larger than the range");
	expect(!rangeContains(e, b, b, 1), "inverted range");
	expect(rangeContains(b, e, b, 0), "empty read inside the range");
	return failures == 0 ? 0 : 1;
}
