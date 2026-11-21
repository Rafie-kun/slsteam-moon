#pragma once

#include <cstddef>
#include <cstdint>


template<typename T>
class CUtlMemory
{
public:

	T* base;
	uint32_t alloc;
	uint32_t growSize;
};

template<typename T>
class CUtlVector
{
public:

	CUtlMemory<T> memory;
	uint32_t size;

	constexpr T* at(uint32_t index)
	{
		if (index >= size)
		{
			return nullptr;
		}

		return &memory.base[index];
	}
};

class CUtlBuffer
{
public:

#if defined(__x86_64__)
	// steamrt64 layout from AceSLS/SLSsteam; the trailing function pointers
	// are kept only so sizeof matches the client's 0x40 bytes.
	CUtlMemory<uint8_t> mem;	//0x0
	int32_t get;				//0x10
	int32_t put;				//0x14
	int32_t offset;				//0x18
	uint32_t flags;				//0x1C
	uint8_t __pad0x20[0x20];	//0x20
#else
	CUtlMemory<uint8_t> mem;	//0x0
	int32_t get;				//0xC
	int32_t put;				//0x10
	int32_t offset;				//0x14
	uint32_t flags;				//0x1A
	uint8_t __pad0x1B[0x8];		//0x1B

#endif

	constexpr bool hasBytes(const std::size_t count) const
	{
		return mem.base && put >= 0 &&
			static_cast<std::size_t>(put) >= count;
	}
}; //0x24

#if defined(__x86_64__)
static_assert(sizeof(CUtlBuffer) == 0x40, "CUtlBuffer layout drift");
#endif
