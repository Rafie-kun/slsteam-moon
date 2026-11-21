#pragma once

#include <cstdint>

// Sanity bounds for a pointer read from a Steam-owned struct whose layout we
// only infer.  A mis-read field shows up as a small integer or as a sentinel
// (0xFFFFFFFF on i386); dereferencing it takes the whole client down, so the
// hooks reject it first.  The upper bound is the user address space of the
// build: 0xFFFFF000 would reject every 64-bit heap pointer.  The canonical
// x86_64 limit no longer catches 0xFFFFFFFF, which is only an i386 sentinel;
// the all-ones value is still rejected.
namespace Addr
{
	inline constexpr std::uintptr_t kMinUser = 0x10000u;
	inline constexpr std::uintptr_t kMaxUser =
		sizeof(std::uintptr_t) == 4
			? static_cast<std::uintptr_t>(0xFFFFF000u)
			: static_cast<std::uintptr_t>(0x0000800000000000ull);

	constexpr bool plausibleUserPointer(std::uintptr_t address) noexcept
	{
		return address >= kMinUser && address < kMaxUser;
	}

	inline bool plausibleUserPointer(const void* pointer) noexcept
	{
		return plausibleUserPointer(reinterpret_cast<std::uintptr_t>(pointer));
	}
}
