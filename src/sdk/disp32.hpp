#pragma once

#include <cstdint>
#include <cstring>

// A Steam instruction that reaches one of its own members encodes the offset
// as a signed 4-byte displacement.  Reading it through a pointer-width load
// only worked while pointers were 4 bytes wide: a negative displacement then
// wrapped onto the right member, on x86_64 the same bits zero-extend to
// `this + ~4 GiB`.
//
// `IClientUtils` is one of those members, at disp -0xc8c on i386 and -0x1028
// on x86_64, so both readers below go through here.
namespace Disp32
{
inline std::int32_t read(const void* field)
{
	std::int32_t value = 0;
	std::memcpy(&value, field, sizeof(value));
	return value;
}

// Address of the member at `offset`.  `T` is the type at the target, which is
// generally not the type of `base` (getUtils starts at a CSteamEngine and
// returns an IClientUtils), so the two are kept independent.
template <typename T>
inline T* at(void* base, std::int32_t offset)
{
	return reinterpret_cast<T*>(
		reinterpret_cast<std::uintptr_t>(base) +
		static_cast<std::uintptr_t>(static_cast<std::intptr_t>(offset)));
}
} // namespace Disp32
