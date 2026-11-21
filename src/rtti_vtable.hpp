#pragma once

// Locate virtual-method implementations through Itanium RTTI instead of code
// bytes.  The i386 table finds the same functions with byte signatures; on
// x86_64 the order of an interface's virtual methods is the stable fact (it is
// the order of the IClient*Map IPC proxies), while the code around each method
// is not.
//
// The resolver only needs read access to the module's loaded segments, so the
// same code runs in-process (pointers already relocated) and offline against
// the ELF on disk (RELATIVE relocations applied from .rela.dyn).  That is what
// lets tools/test_patterns_x86_64.cpp prove a slot before it is ever hooked.
//
// Layout facts used (Itanium C++ ABI, x86_64):
//   typeinfo       [vptr][name*]                           __class_type_info
//                  [vptr][name*][base typeinfo*]           __si_class_type_info
//                  [vptr][name*][u32 flags][u32 n]
//                  { [base typeinfo*][i64 offset<<8|flags] } * n   __vmi_class_type_info
//   vtable group   [offset_to_top][typeinfo*][slot 0][slot 1]...  (vtable pointer -> slot 0)
//
// A secondary base (multiple inheritance) has its own vtable in the group whose
// offset_to_top is minus the base's offset inside the class.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace RttiVtable
{
	struct Segment
	{
		std::uint64_t va = 0;
		const std::uint8_t* data = nullptr;
		std::uint64_t size = 0;
		bool executable = false;
	};

	class Image
	{
	public:
		std::vector<Segment> segments;
		// Load address of the module (0 offline, where addresses are link-time).
		std::uint64_t base = 0;
		// Offline only: RELATIVE relocations (address -> value).  In a loaded
		// module the stored pointers are already absolute.
		const std::unordered_map<std::uint64_t, std::uint64_t>* relocations = nullptr;

		const Segment* segmentAt(std::uint64_t va, std::uint64_t length = 1) const
		{
			for (const Segment& segment : segments)
			{
				if (va >= segment.va && length <= segment.size && va - segment.va <= segment.size - length)
					return &segment;
			}
			return nullptr;
		}

		std::optional<std::uint64_t> raw(std::uint64_t va) const
		{
			const Segment* segment = segmentAt(va, 8);
			if (segment == nullptr)
				return std::nullopt;
			std::uint64_t value = 0;
			std::memcpy(&value, segment->data + (va - segment->va), sizeof(value));
			return value;
		}

		std::optional<std::uint32_t> raw32(std::uint64_t va) const
		{
			const Segment* segment = segmentAt(va, 4);
			if (segment == nullptr)
				return std::nullopt;
			std::uint32_t value = 0;
			std::memcpy(&value, segment->data + (va - segment->va), sizeof(value));
			return value;
		}

		// Pointer-sized value as the running process would see it.
		std::optional<std::uint64_t> pointer(std::uint64_t va) const
		{
			if (relocations != nullptr)
			{
				const auto it = relocations->find(va);
				if (it != relocations->end())
					return it->second;
			}
			return raw(va);
		}

		bool isCode(std::uint64_t va) const
		{
			const Segment* segment = segmentAt(va);
			return segment != nullptr && segment->executable;
		}

		std::optional<std::string_view> cstring(std::uint64_t va, std::size_t maxLength = 160) const
		{
			const Segment* segment = segmentAt(va);
			if (segment == nullptr || segment->executable)
				return std::nullopt;
			const char* begin = reinterpret_cast<const char*>(segment->data + (va - segment->va));
			const std::size_t available = static_cast<std::size_t>(segment->size - (va - segment->va));
			const std::size_t limit = available < maxLength ? available : maxLength;
			const void* end = std::memchr(begin, 0, limit);
			if (end == nullptr)
				return std::nullopt;
			return std::string_view(begin, static_cast<std::size_t>(static_cast<const char*>(end) - begin));
		}

		// Every 8-aligned data location whose pointer value equals `target`.
		std::vector<std::uint64_t> referencesTo(std::uint64_t target) const
		{
			std::vector<std::uint64_t> found;
			for (const Segment& segment : segments)
			{
				if (segment.executable)
					continue;
				const std::uint64_t first = (segment.va + 7) & ~std::uint64_t{7};
				for (std::uint64_t va = first; va + 8 <= segment.va + segment.size; va += 8)
				{
					const auto value = pointer(va);
					if (value && *value == target)
						found.push_back(va);
				}
			}
			return found;
		}
	};

	inline bool looksLikeTypeName(std::string_view text)
	{
		return text.size() >= 2 && text[0] >= '1' && text[0] <= '9';
	}

	// Typeinfo address of the class with this mangled name ("5CUser"), if exactly
	// one typeinfo carries it.
	inline std::optional<std::uint64_t> findTypeinfo(const Image& image, std::string_view mangledName)
	{
		std::vector<std::uint64_t> nameAddresses;
		for (const Segment& segment : image.segments)
		{
			if (segment.executable || segment.size < mangledName.size() + 1)
				continue;
			const char* base = reinterpret_cast<const char*>(segment.data);
			const char* cursor = base;
			const char* const end = base + segment.size;
			while (cursor < end)
			{
				const void* hit = memmem(cursor, static_cast<std::size_t>(end - cursor), mangledName.data(), mangledName.size());
				if (hit == nullptr)
					break;
				const char* at = static_cast<const char*>(hit);
				cursor = at + 1;
				if (at + mangledName.size() >= end || at[mangledName.size()] != '\0')
					continue;
				// Must start a string, not end a longer mangled name.
				if (at != base && at[-1] != '\0')
					continue;
				nameAddresses.push_back(segment.va + static_cast<std::uint64_t>(at - base));
			}
		}

		std::vector<std::uint64_t> typeinfos;
		for (const std::uint64_t nameAddress : nameAddresses)
		{
			for (const std::uint64_t location : image.referencesTo(nameAddress))
			{
				// [vptr][name*]: the word before the name must point into module data
				// (the type_info vtable).  A bare non-zero test also accepts unrelated
				// tables that merely hold a pointer to the same string.
				// .rela.dyn entries are {offset, info, addend}: an addend equal to the
				// name address sits right after an `info` of 8, which this rejects
				// (a vtable address is never that small).
				const auto vptr = image.pointer(location - 8);
				if (!vptr || *vptr < 0x1000)
					continue;
				const Segment* target = image.segmentAt(*vptr);
				if (target != nullptr && !target->executable)
					typeinfos.push_back(location - 8);
			}
		}
		if (typeinfos.size() != 1)
			return std::nullopt;
		return typeinfos.front();
	}

	// Offset of base class `baseTypeinfo` inside the class whose typeinfo is
	// `classTypeinfo`: 0 for a single-inheritance base, the vmi entry otherwise.
	inline std::optional<std::int64_t> baseOffset(
		const Image& image, std::uint64_t classTypeinfo, std::uint64_t baseTypeinfo)
	{
		const auto flagsAndCount = image.raw(classTypeinfo + 0x10);
		if (flagsAndCount)
		{
			const std::uint32_t flags = static_cast<std::uint32_t>(*flagsAndCount & 0xFFFFFFFFu);
			const std::uint32_t count = static_cast<std::uint32_t>(*flagsAndCount >> 32);
			if (flags < 4 && count >= 1 && count <= 32)
			{
				for (std::uint32_t k = 0; k < count; ++k)
				{
					const auto base = image.pointer(classTypeinfo + 0x18 + 16ull * k);
					const auto offsetFlags = image.raw(classTypeinfo + 0x20 + 16ull * k);
					if (base && offsetFlags && *base == baseTypeinfo)
						return static_cast<std::int64_t>(*offsetFlags) >> 8;
				}
				return std::nullopt;
			}
		}
		const auto single = image.pointer(classTypeinfo + 0x10);
		if (single && *single == baseTypeinfo)
			return 0;
		return std::nullopt;
	}

	// Slot function of the vtable that `className` uses for its `baseName` base
	// (empty baseName: its own primary vtable).  Nullopt unless every step is
	// unambiguous and the slot points into code.
	inline std::optional<std::uint64_t> slotFunction(
		const Image& image,
		std::string_view className,
		std::string_view baseName,
		unsigned slot)
	{
		const auto classInfo = findTypeinfo(image, className);
		if (!classInfo)
			return std::nullopt;

		std::int64_t offsetToTop = 0;
		if (!baseName.empty())
		{
			const auto baseInfo = findTypeinfo(image, baseName);
			if (!baseInfo)
				return std::nullopt;
			const auto offset = baseOffset(image, *classInfo, *baseInfo);
			if (!offset)
				return std::nullopt;
			offsetToTop = -*offset;
		}

		std::optional<std::uint64_t> table;
		for (const std::uint64_t location : image.referencesTo(*classInfo))
		{
			const auto top = image.raw(location - 8);
			const auto first = image.pointer(location + 8);
			if (!top || static_cast<std::int64_t>(*top) != offsetToTop || !first || !image.isCode(*first))
				continue;
			if (table)
				return std::nullopt; // two vtables with the same offset: not unique
			table = location + 8;
		}
		if (!table)
			return std::nullopt;

		const auto function = image.pointer(*table + 8ull * slot);
		if (!function || !image.isCode(*function))
			return std::nullopt;
		return function;
	}

	// `sub rdi,imm ; jmp rel32` is the adjustor thunk of a secondary vtable: the
	// function worth hooking is the one it jumps to.  Anything else is returned
	// unchanged.
	inline std::uint64_t followAdjustorThunk(const Image& image, std::uint64_t function)
	{
		for (int depth = 0; depth < 2; ++depth)
		{
			const Segment* segment = image.segmentAt(function, 12);
			if (segment == nullptr || !segment->executable)
				break;
			const std::uint8_t* code = segment->data + (function - segment->va);
			std::size_t adjustLength = 0;
			if (code[0] == 0x48 && code[1] == 0x81 && code[2] == 0xEF)
				adjustLength = 7; // sub rdi, imm32
			else if (code[0] == 0x48 && code[1] == 0x83 && code[2] == 0xEF)
				adjustLength = 4; // sub rdi, imm8
			if (adjustLength == 0 || code[adjustLength] != 0xE9)
				break;
			std::int32_t displacement = 0;
			std::memcpy(&displacement, code + adjustLength + 1, sizeof(displacement));
			const std::uint64_t target = function + adjustLength + 5 + static_cast<std::int64_t>(displacement);
			if (!image.isCode(target))
				break;
			function = target;
		}
		return function;
	}
}

namespace RttiVtable
{
	// Entry point of the function containing `va`, from the module's own
	// .eh_frame_hdr binary-search table (PT_GNU_EH_FRAME).  The same table the
	// unwinder uses, so it is exact for every function that can throw or be
	// unwound through -- all of them in this client.
	inline std::optional<std::uint64_t> functionStart(const Image& image, std::uint64_t va)
	{
		const auto phoff = image.raw(image.base + 0x20);
		const auto shape = image.raw32(image.base + 0x34); // e_ehsize | e_phentsize<<16
		const auto count = image.raw32(image.base + 0x38); // e_phnum | e_shentsize<<16
		const auto magic = image.raw32(image.base);
		if (!phoff || !shape || !count || !magic || *magic != 0x464C457Fu)
			return std::nullopt;
		const std::uint32_t entrySize = *shape >> 16;
		const std::uint32_t entries = *count & 0xFFFFu;

		std::optional<std::uint64_t> header;
		for (std::uint32_t i = 0; i < entries; ++i)
		{
			const std::uint64_t phdr = image.base + *phoff + static_cast<std::uint64_t>(i) * entrySize;
			const auto type = image.raw32(phdr);
			const auto address = image.raw(phdr + 16);
			if (type && *type == 0x6474E550u && address)
			{
				header = image.base + *address;
				break;
			}
		}
		if (!header)
			return std::nullopt;

		const auto encodings = image.raw32(*header);
		const auto fdeCount = image.raw32(*header + 8);
		// 0x1b = pcrel|sdata4 (eh_frame_ptr), 0x03 = udata4 (count), 0x3b = datarel|sdata4.
		if (!encodings || *encodings != 0x3B031B01u || !fdeCount)
			return std::nullopt;

		std::int64_t low = 0;
		std::int64_t high = static_cast<std::int64_t>(*fdeCount) - 1;
		std::optional<std::uint64_t> best;
		while (low <= high)
		{
			const std::int64_t mid = (low + high) / 2;
			const auto initial = image.raw32(*header + 12 + static_cast<std::uint64_t>(mid) * 8);
			if (!initial)
				return std::nullopt;
			const std::uint64_t start = *header + static_cast<std::int64_t>(static_cast<std::int32_t>(*initial));
			if (start <= va)
			{
				best = start;
				low = mid + 1;
			}
			else
				high = mid - 1;
		}
		return best;
	}

	// Functions that load the address of the exact C string `text` with a
	// RIP-relative lea.
	inline std::vector<std::uint64_t> functionsReferencingString(const Image& image, std::string_view text)
	{
		std::vector<std::uint64_t> starts;
		for (const Segment& data : image.segments)
		{
			if (data.executable || data.size < text.size() + 1)
				continue;
			const char* base = reinterpret_cast<const char*>(data.data);
			const char* cursor = base;
			const char* const end = base + data.size;
			while (cursor < end)
			{
				const void* hit = memmem(cursor, static_cast<std::size_t>(end - cursor), text.data(), text.size());
				if (hit == nullptr)
					break;
				const char* at = static_cast<const char*>(hit);
				cursor = at + 1;
				if (at + text.size() >= end || at[text.size()] != '\0' || (at != base && at[-1] != '\0'))
					continue;
				const std::uint64_t target = data.va + static_cast<std::uint64_t>(at - base);

				for (const Segment& code : image.segments)
				{
					if (!code.executable || code.size < 8)
						continue;
					for (std::uint64_t i = 0; i + 7 <= code.size; ++i)
					{
						const std::uint8_t* insn = code.data + i;
						if ((insn[0] != 0x48 && insn[0] != 0x4C) || insn[1] != 0x8D || (insn[2] & 0xC7) != 0x05)
							continue;
						std::int32_t displacement = 0;
						std::memcpy(&displacement, insn + 3, sizeof(displacement));
						if (code.va + i + 7 + static_cast<std::int64_t>(displacement) != target)
							continue;
						if (const auto function = functionStart(image, code.va + i))
							starts.push_back(*function);
					}
				}
			}
		}
		return starts;
	}

	// Number of direct `call rel32` / `jmp rel32` instructions that reach `target`.
	inline std::size_t directCallers(const Image& image, std::uint64_t target)
	{
		std::size_t count = 0;
		for (const Segment& code : image.segments)
		{
			if (!code.executable || code.size < 5)
				continue;
			for (std::uint64_t i = 0; i + 5 <= code.size; ++i)
			{
				const std::uint8_t opcode = code.data[i];
				if (opcode != 0xE8 && opcode != 0xE9)
					continue;
				std::int32_t displacement = 0;
				std::memcpy(&displacement, code.data + i + 1, sizeof(displacement));
				if (code.va + i + 5 + static_cast<std::int64_t>(displacement) == target)
					++count;
			}
		}
		return count;
	}

	// The single function that references every string in `texts`; nullopt when
	// none does or when more than one candidate remains.  With `preferMostCalled`,
	// several candidates are resolved by direct-call count, but only when one of
	// them is called strictly more often than all the others (an overload family
	// whose public entry has the call sites).
	inline std::optional<std::uint64_t> functionReferencingAll(
		const Image& image, const std::vector<std::string_view>& texts, bool preferMostCalled = false,
		bool preferFewestCalled = false)
	{
		std::optional<std::vector<std::uint64_t>> common;
		for (const std::string_view text : texts)
		{
			auto found = functionsReferencingString(image, text);
			std::sort(found.begin(), found.end());
			found.erase(std::unique(found.begin(), found.end()), found.end());
			if (!common)
				common = std::move(found);
			else
			{
				std::vector<std::uint64_t> both;
				std::set_intersection(common->begin(), common->end(), found.begin(), found.end(), std::back_inserter(both));
				common = std::move(both);
			}
		}
		if (!common || common->empty())
			return std::nullopt;
		if (common->size() == 1)
			return common->front();
		if (preferFewestCalled)
		{
			std::optional<std::uint64_t> fewest;
			std::size_t fewestCount = 0;
			std::size_t secondFewest = 0;
			bool first = true;
			for (const std::uint64_t candidate : *common)
			{
				const std::size_t calls = directCallers(image, candidate);
				if (first || calls < fewestCount)
				{
					secondFewest = first ? calls : fewestCount;
					fewestCount = calls;
					fewest = candidate;
					first = false;
				}
				else if (calls < secondFewest || secondFewest == fewestCount)
					secondFewest = calls;
			}
			if (!fewest || fewestCount == secondFewest)
				return std::nullopt;
			return fewest;
		}
		if (!preferMostCalled)
			return std::nullopt;
		std::optional<std::uint64_t> best;
		std::size_t bestCount = 0;
		std::size_t runnerUp = 0;
		for (const std::uint64_t candidate : *common)
		{
			const std::size_t calls = directCallers(image, candidate);
			if (calls > bestCount)
			{
				runnerUp = bestCount;
				bestCount = calls;
				best = candidate;
			}
			else if (calls > runnerUp)
				runnerUp = calls;
		}
		if (!best || bestCount == runnerUp)
			return std::nullopt;
		return best;
	}
}

namespace RttiVtable
{
	// R_X86_64_RELATIVE entries (address -> value) read from the module's own dynamic
	// section (DT_RELA/DT_RELASZ).  la_objopen runs BEFORE the loader relocates the
	// object it announces, so the vtable and typeinfo pointers of a module that is
	// still being opened hold link-time zeros; applying its relocations here gives the
	// values they will have, and is a no-op for a module that is already relocated.
	// `image.base` is the load address (0 for a file view).
	inline std::unordered_map<std::uint64_t, std::uint64_t> relativeRelocationsFromDynamic(const Image& image)
	{
		std::unordered_map<std::uint64_t, std::uint64_t> map;
		const auto phoff = image.raw(image.base + 0x20);
		const auto shape = image.raw32(image.base + 0x34);
		const auto count = image.raw32(image.base + 0x38);
		const auto magic = image.raw32(image.base);
		if (!phoff || !shape || !count || !magic || *magic != 0x464C457Fu)
			return map;
		const std::uint32_t entrySize = *shape >> 16;
		const std::uint32_t entries = *count & 0xFFFFu;

		std::uint64_t dynamic = 0;
		for (std::uint32_t i = 0; i < entries; ++i)
		{
			const std::uint64_t phdr = image.base + *phoff + static_cast<std::uint64_t>(i) * entrySize;
			const auto type = image.raw32(phdr);
			const auto address = image.raw(phdr + 16);
			if (type && *type == 2u /* PT_DYNAMIC */ && address)
			{
				dynamic = image.base + *address;
				break;
			}
		}
		if (dynamic == 0)
			return map;

		std::uint64_t rela = 0;
		std::uint64_t relaSize = 0;
		for (std::uint64_t at = dynamic; ; at += 16)
		{
			const auto tag = image.raw(at);
			const auto value = image.raw(at + 8);
			if (!tag || !value || *tag == 0)
				break;
			// Entries are link-time offsets, or already base-adjusted by the loader.
			if (*tag == 7 /* DT_RELA */)
				rela = *value < image.base ? image.base + *value : *value;
			else if (*tag == 8 /* DT_RELASZ */)
				relaSize = *value;
		}
		if (rela == 0 || relaSize == 0)
			return map;

		map.reserve(static_cast<std::size_t>(relaSize / 24));
		for (std::uint64_t at = 0; at + 24 <= relaSize; at += 24)
		{
			const auto offset = image.raw(rela + at);
			const auto info = image.raw(rela + at + 8);
			const auto addend = image.raw(rela + at + 16);
			if (!offset || !info || !addend)
				break;
			if ((*info & 0xFFFFFFFFu) == 8u /* R_X86_64_RELATIVE */)
				map[image.base + *offset] = image.base + *addend;
		}
		return map;
	}
}
