#include "memhlp.hpp"

#include "log.hpp"
#include "utils.hpp"
#include "config.hpp"
#include "pattern_scan.hpp"

#include "libmem/libmem.h"

#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

std::vector<int16_t> MemHlp::patternToBytes(const char* pattern)
{
	auto bytes = std::vector<int16_t>();

	char* start = const_cast<char*>(pattern);
	char* end = start + strlen(pattern);

	while (start < end)
	{
		if (*start == '?')
		{
			bytes.emplace_back(-1);
		}
		else if (*start != ' ')
		{
			bytes.emplace_back(std::strtoul(start, &start, 16));
		}

		start++;
	}

	return bytes;
}

lm_address_t MemHlp::patternScan(const char* pattern, lm_module_t targetModule,
	std::size_t* matchesOut, std::vector<uintptr_t>* allMatches)
{
	const auto bytes = patternToBytes(pattern);

	auto codeSegments = std::map<lm_address_t, lm_address_t>();
	const static auto enumSegments = [](lm_segment_t* seg, lm_void_t* arg) -> lm_bool_t
	{
		auto rSegments = reinterpret_cast<std::map<lm_address_t, lm_address_t>*>(arg);
		// Only executable code segments. LM_PROT_XR is the combined mask
		// (X|R), so `& LM_PROT_XR` is true for ANY readable region — it would
		// pull in rw-p/r--p DATA segments (heaps, glibc arenas) and scan them.
		// Those sit next to PROT_NONE guard pages whose layout is ASLR/glibc
		// dependent, so the linear scan could read into an unmapped page and
		// SIGSEGV on some machines but not others. Require the execute bit.
		if((seg->prot & LM_PROT_XR) == LM_PROT_XR)
		{
			(*rSegments)[seg->base] = seg->base + seg->size;
			//g_pLog->debug("Code section at %p to %p\n", seg->base, seg->base + seg->size);
		}

		return LM_TRUE;
	};

	LM_EnumSegments(enumSegments, &codeSegments);

	if (bytes.empty())
	{
		return LM_ADDRESS_BAD;
	}

	// Every range is swept, never stopped at the first hit: the count is the
	// only thing that distinguishes "this signature identifies its target" from
	// "this signature matches several places and the first one won".
	PatternScanTotal total;
	for(const auto& itm : codeSegments)
	{
		if (targetModule.base > itm.second)
		{
			continue;
		}
		if (targetModule.base + targetModule.size < itm.first)
		{
			continue;
		}

		// Every match address is retained (up to the cap) so a caller with a
		// relative follow mode can prove convergence instead of guessing.
		accumulateScan(total,
			scanPatternRange(bytes, itm.first, itm.second, true,
				allMatches, kMaxConvergenceCandidates));
	}

	if (matchesOut != nullptr)
	{
		*matchesOut = total.matches;
	}
	if (!total.resolved())
	{
		return LM_ADDRESS_BAD;
	}
	return static_cast<lm_address_t>(total.address);
}

lm_address_t MemHlp::patternScan(const char* pattern, lm_module_t targetModule)
{
	return patternScan(pattern, targetModule, nullptr, nullptr);
}

MemHlp::SignatureSearchResult MemHlp::searchSignatureDetailed(
	const char* name,
	const char* signature,
	lm_module_t module,
	SigFollowMode mode,
	const int16_t* extraData,
	size_t extraDataSize
)
{
	// Keep the original match separate from the followed target.  Relative and
	// prologue resolvers deliberately return a different address, but the
	// original signature is the proof that a local catalog must re-check.
	SignatureSearchResult result;
	std::size_t matches = 0;
	std::vector<uintptr_t> allMatches;
	result.match = patternScan(signature, module, &matches, &allMatches);
	result.target = result.match;
	result.matches = matches;
	if (matches > 1)
	{
		// Several matches are only usable when they are call sites of one
		// function: follow each and require a single shared target.  This is the
		// producer's `relative-convergence` verdict, reproduced here so the
		// client's policy is identical to the audit's.
		std::optional<lm_address_t> converged;
		if (mode == SigFollowMode::Relative && matches <= kMaxConvergenceCandidates
		    && allMatches.size() == matches)
		{
			std::vector<uintptr_t> targets;
			targets.reserve(allMatches.size());
			for (const uintptr_t candidate : allMatches)
			{
				const lm_address_t followed =
					MemHlp::getJmpTarget(static_cast<lm_address_t>(candidate));
				if (followed == LM_ADDRESS_BAD)
				{
					targets.clear();
					break;
				}
				targets.push_back(static_cast<uintptr_t>(followed));
			}
			if (const auto single = convergedTarget(targets))
			{
				converged = static_cast<lm_address_t>(*single);
			}
		}

		if (converged)
		{
			// Report the site that reaches it, so the local cache records a
			// match whose signature bytes still prove this resolution.
			result.match = static_cast<lm_address_t>(allMatches.front());
			result.target = *converged;
			g_pLog->info("Signature for '%s' matched %zu call sites converging on %p\n",
			             name, matches, result.target);
			return result;
		}

		// Named and at warn level: this is a signature that has to be tightened,
		// not a transient condition, and the dependent feature is now off.
		g_pLog->warn("Signature for '%s' matched %zu times without converging; "
		             "refusing to resolve it\n", name, matches);
		result.match = LM_ADDRESS_BAD;
		result.target = LM_ADDRESS_BAD;
		return result;
	}
	if (result.match == LM_ADDRESS_BAD)
	{
		g_pLog->debug("Unable to find signature for %s!\n", name);
	}
	else
	{
		switch (mode)
		{
			case SigFollowMode::Relative:
				g_pLog->debug("Resolving relative of %s at %p\n", name, result.match);
				result.target = MemHlp::getJmpTarget(result.match);
				break;

			case SigFollowMode::PrologueUpwards:
				g_pLog->debug("Searching function prologue of %s from %p\n", name, result.match);
				{
					lm_segment_t matchSegment{};
					if (!LM_FindSegment(result.match, &matchSegment))
					{
						g_pLog->warn("Unable to find matched segment for %s\n", name);
						result.target = LM_ADDRESS_BAD;
						break;
					}
					result.target = MemHlp::findPrologue(
						result.match,
						MemHlp::prologueLowerBound(module.base, matchSegment.base),
						extraData, extraDataSize
					);
				}
				break;

			default:
				break;
		}

		g_pLog->debug("%s at %p\n", name, result.target);
	}

	return result;
}

lm_address_t MemHlp::searchSignature(
	const char* name,
	const char* signature,
	lm_module_t module,
	SigFollowMode mode,
	const int16_t* extraData,
	size_t extraDataSize
)
{
	return searchSignatureDetailed(name, signature, module, mode, extraData, extraDataSize).target;
}

lm_address_t MemHlp::searchSignature(const char* name, const char* signature, lm_module_t module, SigFollowMode mode)
{
	return MemHlp::searchSignature(name, signature, module, mode, nullptr, 0);
}

lm_address_t MemHlp::searchSignature(const char* name, const char* signature, lm_module_t module)
{
	return searchSignature(name, signature, module, SigFollowMode::None);
}

lm_address_t MemHlp::getJmpTarget(lm_address_t address)
{
	lm_inst_t inst;
	if (!LM_Disassemble(address, &inst)) //Should not happen if we land in a code section
	{
		g_pLog->debug("Failed to disassemble code at %p!");
		return LM_ADDRESS_BAD;
	}

	g_pLog->debug("Resolved to %s %s\n", inst.mnemonic, inst.op_str);

	if (strcmp(inst.mnemonic, "jmp") != 0 && strcmp(inst.mnemonic, "call") != 0)
		return LM_ADDRESS_BAD;

	const lm_address_t target = parseJumpTargetOperand(inst.op_str);
	if (target == LM_ADDRESS_BAD)
	{
		g_pLog->debug("Unsupported jump/call operand '%s'\n", inst.op_str);
		return LM_ADDRESS_BAD;
	}
	return target;
}

lm_address_t MemHlp::findPrologue(lm_address_t address, lm_address_t lowerBound,
                                   const int16_t* prologueBytes, lm_size_t prologueSize)
{
	constexpr unsigned int scanSize = 0x10000;

	for(unsigned int i = 0u; i < scanSize; i++)
	{
		if (!prologueWindowWithin(address, lowerBound, i, prologueSize))
			break;

		bool found = true;
		for(unsigned int j = 0u; j < prologueSize; j++)
		{
			if (!prologueByteMatches(
				prologueBytes[j], *reinterpret_cast<lm_byte_t*>(address - i - j)))
			{
				found = false;
				break;
			}
		}

		if (found)
		{
			lm_address_t prol = address - i - prologueSize + 1; //Add 1 byte back since bytesSize would be to big otherwise
			g_pLog->debug("Prologue found at %p\n", prol);
			return prol;
		}
	}

	g_pLog->debug("Unable to find prologue after going up %p bytes!\n", scanSize);
	return LM_ADDRESS_BAD;
}

bool MemHlp::fixRelativeInstructions(const lm_address_t oldLocation, const lm_address_t newLocation, const std::size_t size)
{
	const lm_arch_t arch = LM_GetArchitecture();
	lm_inst_t* instsBuf = nullptr;
	lm_byte_t* newBytes = nullptr;

	// The last argument is the runtime address the bytes originally lived at,
	// so the disassembler resolves relative operands against the old location.
	const std::size_t count = LM_DisassembleEx(newLocation, arch, size, 0, oldLocation, &instsBuf);
	if (count == 0 || !instsBuf)
	{
		g_pLog->warn("Unable to disassemble trampoline at %p\n", reinterpret_cast<void*>(newLocation));
		return false;
	}

	std::string asmText;
	for (std::size_t i = 0; i < count; i++)
	{
		if (!asmText.empty())
			asmText += " ; ";
		asmText += instsBuf[i].mnemonic;
		asmText += ' ';
		asmText += instsBuf[i].op_str;
	}

	const std::size_t newSize = LM_AssembleEx(asmText.c_str(), arch, newLocation, &newBytes);

	bool success = false;
	if (newSize == 0 || !newBytes)
	{
		g_pLog->warn("Unable to re-assemble trampoline '%s'\n", asmText.c_str());
	}
	else if (newSize > size)
	{
		g_pLog->warn("Fixed trampoline instructions do not fit (%zu > %zu bytes)\n", newSize, size);
	}
	else
	{
		lm_prot_t oldProt;
		LM_ProtMemory(newLocation, size, LM_PROT_XRW, &oldProt);
		memcpy(reinterpret_cast<void*>(newLocation), newBytes, newSize);
		LM_ProtMemory(newLocation, size, oldProt, nullptr);
		g_pLog->debug("Rewrote trampoline at %p with %s\n", reinterpret_cast<void*>(newLocation), asmText.c_str());
		success = true;
	}

	LM_FreeInstructions(instsBuf);
	if (newBytes)
		LM_FreePayload(newBytes);
	return success;
}

#if defined(__x86_64__)
// i386-only: it decodes `call __x86.get_pc_thunk` with a 32-bit operand read.
// A caller that reaches this on x86_64 needs fixRelativeInstructions instead;
// fail loudly rather than patch the trampoline with i386 assumptions.
bool MemHlp::fixPICThunkCall(const char* name, lm_address_t, lm_address_t)
{
	g_pLog->warn("fixPICThunkCall(%s) is i386-only; use fixRelativeInstructions on x86_64\n", name);
	return false;
}
#else
bool MemHlp::fixPICThunkCall(const char* name, lm_address_t fn, lm_address_t tramp)
{
	g_pLog->debug("Fixing PIC thunks for %s's trampoline\n", name);
	constexpr unsigned int maxBytes = 0x5; //Minimum bytes needed to detour a function, so our tramp will at least be of this size
	
	lm_inst_t inst;
	for(unsigned int curTrampOffset = 0; curTrampOffset <= maxBytes; )
	{
		lm_address_t startAddress = tramp + curTrampOffset;

		if (!LM_Disassemble(startAddress, &inst))
		{
			g_pLog->debug("Unable to dissassemble code at %p\n", tramp + curTrampOffset);
			return false;
		}
		
		curTrampOffset += inst.size;
		g_pLog->debug("%p: %s %s\n", inst.address, inst.mnemonic, inst.op_str);
		
		if (strcmp(inst.mnemonic, "call") != 0)
			continue;

		//Calculate the call address manually with it's original location
		lm_address_t followAddress = fn + curTrampOffset + *reinterpret_cast<lm_address_t*>(startAddress + 1);
		bool isIPCThunk = true;
		char newInstr[256] = {};

		for(unsigned int i = 0; i < 2; i++) //Dissassemble next 2 instructions and check if they're an actual IPC thunk call
		{
			if (!LM_Disassemble(followAddress, &inst))
			{
				g_pLog->debug("Unable to dissassemble code at %p\n", followAddress);
				return false;
			}

			followAddress += inst.size;

			g_pLog->debug("%p: %s %s\n", inst.address, inst.mnemonic, inst.op_str);

			//Can not declare in switch statement
			auto splits = std::vector<std::string>();
			lm_address_t retAddress = LM_ADDRESS_BAD;
			switch(i)
			{
				case 0:
					if (strcmp(inst.mnemonic, "mov") != 0)
						isIPCThunk = false;
					
					if (inst.op_str[0] == '\0')
					{
						isIPCThunk = false;
						break;
					}
					splits = Utils::strsplit(inst.op_str, ",");
					if (splits.empty() || splits.front().empty())
					{
						isIPCThunk = false;
						break;
					}
					retAddress = fn + curTrampOffset; //No need to add any bytes here, since i += inst.size in the outer loop takes care of that
					if (!formatPICThunkInstruction(
						newInstr, sizeof(newInstr), inst.mnemonic,
						splits.front().c_str(), retAddress))
					{
						g_pLog->debug("Unable to format PIC thunk instruction\n");
						return false;
					}
					break;

				case 1:
					if (strcmp(inst.mnemonic, "ret") != 0)
						isIPCThunk = false;
					break;
			}

			if (!isIPCThunk)
				break;
		}

		if (!isIPCThunk)
			continue;

		if(!LM_Assemble(newInstr, &inst))
		{
			printf("Unable to assemble instruction %s!\n", newInstr);
			return false;
		}

		lm_prot_t oldProt;
		LM_ProtMemory(startAddress, inst.size, LM_PROT_XRW, &oldProt);
		LM_WriteMemory(startAddress, inst.bytes, inst.size);
		LM_ProtMemory(startAddress, inst.size, oldProt, nullptr);
		g_pLog->debug("Replaced PIC thunk call for %s at %p with %s\n", name, followAddress, newInstr);
		return true;
	}

	return false;
}
#endif


const char* MemHlp::getTypeName(void* pClass)
{
	const lm_address_t vft = *reinterpret_cast<lm_address_t*>(pClass);
	const lm_address_t typeInfo = *reinterpret_cast<lm_address_t*>(vft - sizeof(lm_address_t));
	const char* name = *reinterpret_cast<const char**>(typeInfo + sizeof(lm_address_t));

	return name;
}

lm_address_t MemHlp::searchOffsetByTypeName(const void* pClass, const std::string& name)
{
	constexpr lm_address_t MAX_OFFSET = 0x10000;

	struct Range
	{
		lm_address_t begin;
		lm_address_t end;
	};
	std::vector<Range> readable;
	LM_EnumSegments([](lm_segment_t* seg, lm_void_t* arg) -> lm_bool_t
	{
		if (seg->prot & LM_PROT_R)
			static_cast<std::vector<Range>*>(arg)->push_back({seg->base, seg->end});
		return LM_TRUE;
	}, &readable);

	const auto canRead = [&](lm_address_t address, std::size_t size)
	{
		for (const Range& range : readable)
		{
			if (MemHlp::rangeContains(range.begin, range.end, address, size))
				return true;
		}
		return false;
	};

	const lm_address_t base = reinterpret_cast<lm_address_t>(pClass);
	for (lm_address_t off = sizeof(lm_address_t); off <= MAX_OFFSET; off += sizeof(lm_address_t))
	{
		if (!canRead(base + off, sizeof(lm_address_t)))
			continue;
		const lm_address_t vft = *reinterpret_cast<const lm_address_t*>(base + off);
		if (!canRead(vft - sizeof(lm_address_t), sizeof(lm_address_t)))
			continue;
		const lm_address_t typeInfo = *reinterpret_cast<const lm_address_t*>(vft - sizeof(lm_address_t));
		if (!canRead(typeInfo, 2 * sizeof(lm_address_t)))
			continue;
		const lm_address_t namePtr = *reinterpret_cast<const lm_address_t*>(typeInfo + sizeof(lm_address_t));
		if (!canRead(namePtr, name.size() + 1))
			continue;
		if (std::memcmp(reinterpret_cast<const char*>(namePtr), name.c_str(), name.size() + 1) == 0)
		{
			g_pLog->debug("Found member of type %s at +0x%lx\n", name.c_str(), off);
			return off;
		}
	}

	g_pLog->warn("No member of type %s found within 0x%lx bytes\n", name.c_str(), MAX_OFFSET);
	return LM_ADDRESS_BAD;
}
