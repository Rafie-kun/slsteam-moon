#include "detour_x64.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace
{
	constexpr std::int64_t kReach = 0x7FFF0000;  // keep clear of the +-2 GiB rel32 limit
	constexpr std::size_t kPage = 0x1000;
	constexpr std::size_t kIslandSize = 16;       // FF 25 00 00 00 00 <abs64>
	constexpr std::size_t kMaxDisplaced = 0x30;

	struct Record
	{
		lm_address_t target;
		std::size_t displaced;
		std::vector<std::uint8_t> original;
	};
	std::unordered_map<lm_address_t, Record> g_records;

	void fail(std::string* error, const std::string& message)
	{
		if (error)
			*error = message;
	}

	bool fits32(std::int64_t value) noexcept
	{
		return value >= INT32_MIN && value <= INT32_MAX;
	}

	// One page within reach of `target`, or nullptr.
	std::uint8_t* allocateNear(lm_address_t target)
	{
		const std::int64_t base = static_cast<std::int64_t>(target & ~static_cast<lm_address_t>(kPage - 1));
		for (std::int64_t step = 1; step <= 2048; ++step)
		{
			for (const std::int64_t sign : {1, -1})
			{
				const std::int64_t hint = base + sign * step * 0x100000;
				if (hint <= 0x10000 || std::llabs(hint - static_cast<std::int64_t>(target)) > kReach)
					continue;
				void* page = mmap(reinterpret_cast<void*>(hint), kPage, PROT_READ | PROT_WRITE | PROT_EXEC,
				                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
				if (page == MAP_FAILED)
					continue;
				if (std::llabs(reinterpret_cast<std::int64_t>(page) - static_cast<std::int64_t>(target)) <= kReach)
					return static_cast<std::uint8_t*>(page);
				munmap(page, kPage);
			}
		}
		return nullptr;
	}

	void emitAbsoluteJump(std::uint8_t* at, lm_address_t destination)
	{
		static const std::uint8_t head[6] = {0xFF, 0x25, 0, 0, 0, 0};
		std::memcpy(at, head, sizeof(head));
		std::memcpy(at + 6, &destination, sizeof(destination));
	}

	// Parse "[rip + 0x1f]" / "[rip - 0x20]" from a Capstone operand string.
	bool parseRipDisplacement(const char* operands, std::int64_t& out)
	{
		const char* rip = std::strstr(operands, "rip");
		if (!rip)
			return false;
		const char* cursor = rip + 3;
		while (*cursor == ' ')
			++cursor;
		std::int64_t sign = 1;
		if (*cursor == '+')
			++cursor;
		else if (*cursor == '-')
		{
			sign = -1;
			++cursor;
		}
		else
			return false;
		while (*cursor == ' ')
			++cursor;
		char* end = nullptr;
		const unsigned long long value = std::strtoull(cursor, &end, 0);
		if (end == cursor)
			return false;
		out = sign * static_cast<std::int64_t>(value);
		return true;
	}

	bool isBranch(const char* mnemonic)
	{
		return mnemonic[0] == 'j' || std::strncmp(mnemonic, "call", 4) == 0 || std::strncmp(mnemonic, "loop", 4) == 0;
	}

	// Copy one instruction to `out` (which sits at `newAddress`), fixing what depends on
	// its address.  Returns the number of bytes written, 0 on failure.
	std::size_t relocate(const lm_inst_t& inst, std::uint8_t* out, lm_address_t newAddress, std::string* error)
	{
		const std::uint8_t* bytes = inst.bytes;
		const std::size_t size = inst.size;
		const lm_address_t oldAddress = inst.address;

		if (isBranch(inst.mnemonic) && std::strncmp(inst.op_str, "0x", 2) == 0)
		{
			const lm_address_t target = std::strtoull(inst.op_str, nullptr, 16);
			const std::uint8_t op = bytes[0];
			const std::uint8_t op2 = size > 1 ? bytes[1] : 0;
			std::size_t length = 0;
			std::uint8_t encoded[6];
			if (op == 0xE8 || op == 0xE9)
			{
				encoded[0] = op;
				length = 5;
			}
			else if (op == 0xEB)
			{
				encoded[0] = 0xE9;       // jmp rel8 -> jmp rel32
				length = 5;
			}
			else if (op >= 0x70 && op <= 0x7F)
			{
				encoded[0] = 0x0F;       // jcc rel8 -> jcc rel32
				encoded[1] = static_cast<std::uint8_t>(0x80 | (op & 0x0F));
				length = 6;
			}
			else if (op == 0x0F && op2 >= 0x80 && op2 <= 0x8F)
			{
				encoded[0] = 0x0F;
				encoded[1] = op2;
				length = 6;
			}
			else
			{
				fail(error, std::string("cannot relocate branch '") + inst.mnemonic + " " + inst.op_str + "'");
				return 0;
			}
			const std::int64_t rel = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(newAddress + length);
			if (!fits32(rel))
			{
				fail(error, "relative branch target is out of reach of the trampoline");
				return 0;
			}
			const std::int32_t rel32 = static_cast<std::int32_t>(rel);
			std::memcpy(encoded + length - 4, &rel32, sizeof(rel32));
			std::memcpy(out, encoded, length);
			return length;
		}

		std::memcpy(out, bytes, size);
		std::int64_t oldDisplacement = 0;
		if (parseRipDisplacement(inst.op_str, oldDisplacement))
		{
			const std::int32_t pattern = static_cast<std::int32_t>(oldDisplacement);
			std::size_t found = size;
			std::size_t matches = 0;
			for (std::size_t i = 1; i + 4 <= size; ++i)
			{
				if (std::memcmp(out + i, &pattern, sizeof(pattern)) == 0)
				{
					found = i;
					++matches;
				}
			}
			if (matches != 1)
			{
				fail(error, std::string("cannot locate the RIP displacement in '") + inst.mnemonic + " " + inst.op_str + "'");
				return 0;
			}
			const std::int64_t absolute = static_cast<std::int64_t>(oldAddress + size) + oldDisplacement;
			const std::int64_t fresh = absolute - static_cast<std::int64_t>(newAddress + size);
			if (!fits32(fresh))
			{
				fail(error, "RIP-relative target is out of reach of the trampoline");
				return 0;
			}
			const std::int32_t fresh32 = static_cast<std::int32_t>(fresh);
			std::memcpy(out + found, &fresh32, sizeof(fresh32));
		}
		return size;
	}
}

std::optional<DetourX64::Placed> DetourX64::place(lm_address_t target, lm_address_t hook, std::string* error)
{
	if (g_records.contains(target))
	{
		fail(error, "target is already detoured");
		return std::nullopt;
	}

	lm_inst_t* instructions = nullptr;
	const std::size_t count = LM_DisassembleEx(target, LM_GetArchitecture(), kMaxDisplaced, 0, target, &instructions);
	if (count == 0 || !instructions)
	{
		fail(error, "cannot disassemble the target");
		return std::nullopt;
	}

	std::size_t used = 0;
	std::size_t displaced = 0;
	while (used < count && displaced < 5)
		displaced += instructions[used++].size;
	if (displaced < 5)
	{
		LM_FreeInstructions(instructions);
		fail(error, "fewer than 5 bytes of instructions at the target");
		return std::nullopt;
	}

	std::uint8_t* page = allocateNear(target);
	if (!page)
	{
		LM_FreeInstructions(instructions);
		fail(error, "no executable page within 2 GiB of the target");
		return std::nullopt;
	}

	// island: absolute jump to the hook; trampoline right behind it.
	emitAbsoluteJump(page, hook);
	std::uint8_t* trampoline = page + kIslandSize;
	std::size_t written = 0;
	for (std::size_t i = 0; i < used; ++i)
	{
		const std::size_t length = relocate(instructions[i], trampoline + written,
		                                    reinterpret_cast<lm_address_t>(trampoline + written), error);
		if (length == 0)
		{
			LM_FreeInstructions(instructions);
			munmap(page, kPage);
			return std::nullopt;
		}
		written += length;
		if (written + 14 > kPage - kIslandSize)
		{
			LM_FreeInstructions(instructions);
			munmap(page, kPage);
			fail(error, "trampoline does not fit");
			return std::nullopt;
		}
	}
	emitAbsoluteJump(trampoline + written, target + displaced);
	LM_FreeInstructions(instructions);

	const std::int64_t toIsland = static_cast<std::int64_t>(reinterpret_cast<lm_address_t>(page))
	                            - static_cast<std::int64_t>(target + 5);
	if (!fits32(toIsland))
	{
		munmap(page, kPage);
		fail(error, "island is out of reach of the target");
		return std::nullopt;
	}

	Record record{target, displaced, std::vector<std::uint8_t>(displaced)};
	std::memcpy(record.original.data(), reinterpret_cast<void*>(target), displaced);

	std::vector<std::uint8_t> patch(displaced, 0x90);
	patch[0] = 0xE9;
	const std::int32_t rel = static_cast<std::int32_t>(toIsland);
	std::memcpy(patch.data() + 1, &rel, sizeof(rel));

	lm_prot_t oldProtection = LM_PROT_NONE;
	if (!LM_ProtMemory(target, displaced, LM_PROT_XRW, &oldProtection))
	{
		munmap(page, kPage);
		fail(error, "cannot make the target writable");
		return std::nullopt;
	}
	std::memcpy(reinterpret_cast<void*>(target), patch.data(), displaced);
	LM_ProtMemory(target, displaced, oldProtection, nullptr);
	__builtin___clear_cache(reinterpret_cast<char*>(target), reinterpret_cast<char*>(target + displaced));

	g_records.emplace(target, std::move(record));
	return Placed{target, reinterpret_cast<lm_address_t>(trampoline), displaced};
}

bool DetourX64::remove(lm_address_t target)
{
	const auto it = g_records.find(target);
	if (it == g_records.end())
		return false;
	lm_prot_t oldProtection = LM_PROT_NONE;
	if (!LM_ProtMemory(target, it->second.displaced, LM_PROT_XRW, &oldProtection))
		return false;
	std::memcpy(reinterpret_cast<void*>(target), it->second.original.data(), it->second.displaced);
	LM_ProtMemory(target, it->second.displaced, oldProtection, nullptr);
	g_records.erase(it);
	return true;
}
