#include "../src/sdk/disp32.hpp"

#include <cstdint>
#include <cstdio>

static int g_failures = 0;

#define CHECK(cond, msg)                                                      \
	do                                                                        \
	{                                                                         \
		if (!(cond))                                                          \
		{                                                                     \
			std::printf("FAIL: %s\n", msg);                                   \
			g_failures++;                                                     \
		}                                                                     \
	} while (0)

int main()
{
	// IClientUtils sits before the pointer the engine returns; the encoder
	// writes those offsets as two's complement.
	const std::uint8_t negative[] = {0xd8, 0xef, 0xff, 0xff};
	CHECK(Disp32::read(negative) == -0x1028,
	      "a negative displacement is read as signed");
	const std::uint8_t positive[] = {0x58, 0x4a, 0x00, 0x00};
	CHECK(Disp32::read(positive) == 0x4a58,
	      "a positive displacement is unchanged");

	// The member address must be computed with the sign extended, not as a
	// zero-extended 32-bit value added to a 64-bit pointer.
	std::uint8_t object[0x40] = {};
	auto* back = Disp32::at<std::uint32_t>(object + 0x20, -0x10);
	CHECK(reinterpret_cast<std::uint8_t*>(back) == object + 0x10,
	      "a negative offset addresses backwards from the base");
	auto* forward = Disp32::at<std::uint32_t>(object, 0x0c);
	CHECK(reinterpret_cast<std::uint8_t*>(forward) == object + 0x0c,
	      "a positive offset addresses forwards from the base");

	if (g_failures == 0)
	{
		std::printf("test_disp32: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
