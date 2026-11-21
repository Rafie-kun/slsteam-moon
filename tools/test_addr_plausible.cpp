// Pointer plausibility bounds must follow the build's address space.
#include "addr_plausible.hpp"

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
	expect(!Addr::plausibleUserPointer(std::uintptr_t{0}), "null rejected");
	expect(!Addr::plausibleUserPointer(std::uintptr_t{0xFFFF}), "first page rejected");
	expect(Addr::plausibleUserPointer(std::uintptr_t{0x10000}), "lowest accepted address");
	expect(Addr::plausibleUserPointer(std::uintptr_t{0x0804A000u}), "classic i386 mapping accepted");
	expect(!Addr::plausibleUserPointer(~std::uintptr_t{0}), "all-ones sentinel rejected");
	if constexpr (sizeof(void*) == 4)
	{
		expect(!Addr::plausibleUserPointer(static_cast<std::uintptr_t>(0xFFFFFFFFu)),
		       "0xFFFFFFFF sentinel rejected");
	}
	if constexpr (sizeof(void*) == 8)
	{
		expect(Addr::plausibleUserPointer(std::uintptr_t{0x00007F1234567000ull}),
		       "64-bit heap pointer above 4 GiB accepted");
		expect(!Addr::plausibleUserPointer(std::uintptr_t{0xFFFF800000000000ull}),
		       "kernel-half address rejected");
		expect(!Addr::plausibleUserPointer(std::uintptr_t{0x0000800000000000ull}),
		       "first non-canonical address rejected");
	}
	int local = 0;
	expect(Addr::plausibleUserPointer(&local), "a real stack address is plausible");
	return failures == 0 ? 0 : 1;
}
