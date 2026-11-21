// Offline audit of the x86_64 locator table (src/patterns_x86_64.hpp) against a
// real steamrt64 client.  Nothing is loaded or executed: the ELF is read from
// disk and its executable PT_LOAD segments are scanned.
//
// For every locator it asserts:
//   - the module is ELF64 / x86-64;
//   - the signature has exactly one match, or (Relative) several call sites that
//     all reach one target -- the same rule as MemHlp::searchSignatureDetailed;
//   - PrologueUpwards finds its prologue inside the matched segment;
//   - the name is a real Pattern_t in src/patterns.cpp, and the table has no
//     duplicates.
//
//   make test-patterns-x86_64 [STEAMCLIENT64=/path/to/linux64/steamclient.so]

#include "patterns_x86_64.hpp"
#include "pattern_scan.hpp"
#include "rtti_vtable.hpp"
#include "vftableinfo.hpp"

#include <elf.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <unordered_map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void fail(const std::string& message)
{
	std::cerr << "FAIL: " << message << '\n';
	++failures;
}

std::vector<int16_t> parsePattern(std::string_view text)
{
	std::vector<int16_t> bytes;
	std::istringstream stream{std::string(text)};
	std::string token;
	while (stream >> token)
	{
		if (token == "?" || token == "??")
			bytes.push_back(-1);
		else
			bytes.push_back(static_cast<int16_t>(std::strtoul(token.c_str(), nullptr, 16)));
	}
	return bytes;
}

struct Segment
{
	std::uint64_t vaddr;
	const std::uint8_t* begin;
	const std::uint8_t* end;
};

// Function entry points straight from .eh_frame_hdr (sorted binary-search table,
// datarel sdata4): an independent ground truth for "this is a function start".
std::set<std::uint64_t> functionStarts(const std::vector<std::uint8_t>& image, const Elf64_Ehdr& header)
{
	std::set<std::uint64_t> starts;
	if (header.e_shoff == 0 || header.e_shstrndx >= header.e_shnum)
		return starts;
	const auto section = [&](unsigned index)
	{
		Elf64_Shdr sh{};
		std::memcpy(&sh, image.data() + header.e_shoff + static_cast<std::size_t>(index) * header.e_shentsize, sizeof(sh));
		return sh;
	};
	const Elf64_Shdr names = section(header.e_shstrndx);
	for (unsigned i = 0; i < header.e_shnum; ++i)
	{
		const Elf64_Shdr sh = section(i);
		const char* name = reinterpret_cast<const char*>(image.data() + names.sh_offset + sh.sh_name);
		if (std::strcmp(name, ".eh_frame_hdr") != 0)
			continue;
		const std::uint8_t* hdr = image.data() + sh.sh_offset;
		if (hdr[1] != 0x1b || hdr[2] != 0x03 || hdr[3] != 0x3b)
			return starts; // unexpected encoding: no ground truth
		std::int32_t count = 0;
		std::memcpy(&count, hdr + 8, sizeof(count));
		for (std::int32_t k = 0; k < count; ++k)
		{
			std::int32_t initial = 0;
			std::memcpy(&initial, hdr + 12 + static_cast<std::size_t>(k) * 8, sizeof(initial));
			starts.insert(sh.sh_addr + static_cast<std::int64_t>(initial));
		}
	}
	return starts;
}

// R_X86_64_RELATIVE entries of .rela.dyn: what the loader would store in the
// vtable and typeinfo pointers.
std::unordered_map<std::uint64_t, std::uint64_t> relativeRelocations(
	const std::vector<std::uint8_t>& image, const Elf64_Ehdr& header)
{
	std::unordered_map<std::uint64_t, std::uint64_t> map;
	if (header.e_shoff == 0 || header.e_shstrndx >= header.e_shnum)
		return map;
	const auto section = [&](unsigned index)
	{
		Elf64_Shdr sh{};
		std::memcpy(&sh, image.data() + header.e_shoff + static_cast<std::size_t>(index) * header.e_shentsize, sizeof(sh));
		return sh;
	};
	const Elf64_Shdr names = section(header.e_shstrndx);
	for (unsigned i = 0; i < header.e_shnum; ++i)
	{
		const Elf64_Shdr sh = section(i);
		const char* name = reinterpret_cast<const char*>(image.data() + names.sh_offset + sh.sh_name);
		if (std::strcmp(name, ".rela.dyn") != 0)
			continue;
		for (std::uint64_t at = 0; at + sizeof(Elf64_Rela) <= sh.sh_size; at += sizeof(Elf64_Rela))
		{
			Elf64_Rela rela{};
			std::memcpy(&rela, image.data() + sh.sh_offset + at, sizeof(rela));
			if (ELF64_R_TYPE(rela.r_info) == R_X86_64_RELATIVE)
				map[rela.r_offset] = static_cast<std::uint64_t>(rela.r_addend);
		}
	}
	return map;
}

// Strings a proxy method references through RIP-relative lea: the IClient*Map
// slot functions name the IPC method they marshal.
std::set<std::string> referencedNames(const RttiVtable::Image& view, std::uint64_t function, std::uint64_t window = 0x200)
{
	std::set<std::string> names;
	const RttiVtable::Segment* segment = view.segmentAt(function, 16);
	if (segment == nullptr)
		return names;
	const std::uint64_t available = segment->size - (function - segment->va);
	const std::uint64_t length = available < window ? available : window;
	const std::uint8_t* code = segment->data + (function - segment->va);
	for (std::uint64_t i = 0; i + 7 <= length; ++i)
	{
		if ((code[i] != 0x48 && code[i] != 0x4C) || code[i + 1] != 0x8D || (code[i + 2] & 0xC7) != 0x05)
			continue;
		std::int32_t displacement = 0;
		std::memcpy(&displacement, code + i + 3, sizeof(displacement));
		const std::uint64_t target = function + i + 7 + static_cast<std::int64_t>(displacement);
		if (const auto text = view.cstring(target, 96))
			names.insert(std::string(*text));
	}
	return names;
}

std::uint64_t rva(const Segment& segment, const std::uint8_t* at)
{
	return segment.vaddr + static_cast<std::uint64_t>(at - segment.begin);
}
}

// Audit the locators that belong to one module (steamclient.so or steamui.so).
static int audit(const char* modulePath, const char* sourcePath, bool steamUi)
{
	failures = 0;
	const char* argv[3] = { "", modulePath, sourcePath };

	std::ifstream moduleFile(argv[1], std::ios::binary);
	if (!moduleFile)
	{
		std::cerr << "cannot open " << argv[1] << '\n';
		return 2;
	}
	const std::vector<std::uint8_t> image{
		std::istreambuf_iterator<char>(moduleFile), std::istreambuf_iterator<char>()};

	Elf64_Ehdr header{};
	if (image.size() < sizeof(header))
	{
		std::cerr << "not an ELF file\n";
		return 2;
	}
	std::memcpy(&header, image.data(), sizeof(header));
	if (std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0
	    || header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_machine != EM_X86_64)
	{
		std::cerr << "FAIL: " << argv[1] << " is not an ELF64 x86-64 module\n";
		return 1;
	}

	std::vector<Segment> segments;
	for (unsigned i = 0; i < header.e_phnum; ++i)
	{
		Elf64_Phdr ph{};
		const std::size_t at = header.e_phoff + static_cast<std::size_t>(i) * header.e_phentsize;
		if (at + sizeof(ph) > image.size())
			break;
		std::memcpy(&ph, image.data() + at, sizeof(ph));
		if (ph.p_type != PT_LOAD || (ph.p_flags & PF_X) == 0)
			continue;
		if (ph.p_offset + ph.p_filesz > image.size())
			continue;
		segments.push_back({ph.p_vaddr, image.data() + ph.p_offset,
		                    image.data() + ph.p_offset + ph.p_filesz});
	}
	if (segments.empty())
	{
		std::cerr << "FAIL: no executable segment\n";
		return 1;
	}

	// Read-only view for the RTTI resolver: every file-backed PT_LOAD segment.
	const auto relocations = relativeRelocations(image, header);
	RttiVtable::Image view;
	view.relocations = &relocations;
	for (unsigned i = 0; i < header.e_phnum; ++i)
	{
		Elf64_Phdr ph{};
		std::memcpy(&ph, image.data() + header.e_phoff + static_cast<std::size_t>(i) * header.e_phentsize, sizeof(ph));
		if (ph.p_type != PT_LOAD || ph.p_offset + ph.p_filesz > image.size())
			continue;
		RttiVtable::Segment entry;
		entry.va = ph.p_vaddr;
		entry.data = image.data() + ph.p_offset;
		entry.size = ph.p_filesz;
		entry.executable = (ph.p_flags & PF_X) != 0;
		view.segments.push_back(entry);
	}

	// Second view, as the running process sees the module: relocations already
	// stored in memory (load base 0), no side table.  The in-process resolver
	// takes this path, so it must give the same answers.
	std::vector<std::vector<std::uint8_t>> loadedStorage(view.segments.size());
	RttiVtable::Image loaded;
	for (std::size_t i = 0; i < view.segments.size(); ++i)
	{
		const RttiVtable::Segment& source = view.segments[i];
		loadedStorage[i].assign(source.data, source.data + source.size);
		for (const auto& [address, value] : relocations)
		{
			if (address >= source.va && address + 8 <= source.va + source.size)
				std::memcpy(loadedStorage[i].data() + (address - source.va), &value, sizeof(value));
		}
		RttiVtable::Segment entry = source;
		entry.data = loadedStorage[i].data();
		loaded.segments.push_back(entry);
	}

	// Third view, as a module that is still being opened looks to an LD_AUDIT
	// library: raw (unrelocated) file bytes plus the relocations read from its own
	// dynamic section.  This is what la_objopen sees for a module that is not mapped
	// before the hook runs, and must resolve exactly like the other two.
	const auto fromDynamic = RttiVtable::relativeRelocationsFromDynamic(view);
	if (fromDynamic != relocations)
		fail("relocations read from the dynamic section differ from .rela.dyn (" + std::to_string(fromDynamic.size())
		     + " vs " + std::to_string(relocations.size()) + ")");
	RttiVtable::Image opening;
	opening.segments = view.segments;
	opening.relocations = &fromDynamic;

	std::ifstream sourceFile(argv[2]);
	const std::string source{
		std::istreambuf_iterator<char>(sourceFile), std::istreambuf_iterator<char>()};
	if (source.empty())
	{
		std::cerr << "cannot read " << argv[2] << '\n';
		return 2;
	}

	const std::set<std::uint64_t> starts = functionStarts(image, header);
	if (starts.empty())
		std::cerr << "warning: no .eh_frame_hdr table, function entries are not cross-checked\n";

	std::set<std::string> seen;
	std::size_t audited = 0;
	for (const auto& locator : PatternsX64::locators())
	{
		if (locator.steamUi != steamUi)
			continue;
		++audited;
		const std::string name(locator.name);
		if (!seen.insert(name).second)
			fail(name + ": duplicated in the x86_64 table");
		if (source.find("\"" + name + "\"") == std::string::npos)
			fail(name + ": no such Pattern_t in patterns.cpp");

		if (locator.follow == PatternsX64::Follow::StringXref)
		{
			const auto function = RttiVtable::functionReferencingAll(view, locator.xrefStrings, locator.preferMostCalled, locator.preferFewestCalled);
			if (!function)
			{
				fail(name + ": the strings do not resolve to exactly one function");
				continue;
			}
			if (!starts.empty() && starts.count(*function) == 0)
			{
				fail(name + ": string-xref target is not a function entry in .eh_frame");
				continue;
			}
			const auto inProcess = RttiVtable::functionReferencingAll(loaded, locator.xrefStrings, locator.preferMostCalled, locator.preferFewestCalled);
			if (!inProcess || *inProcess != *function)
			{
				fail(name + ": the relocated-memory view resolves differently from the file view");
				continue;
			}
			std::cout << "ok   " << name << "  string xref  target_rva=0x" << std::hex << *function << std::dec << '\n';
			continue;
		}

		if (locator.follow == PatternsX64::Follow::VtableSlot)
		{
			const auto slot = RttiVtable::slotFunction(
				view, locator.rttiClass, locator.rttiBase, static_cast<unsigned>(locator.slot));
			if (!slot)
			{
				fail(name + ": vtable slot " + std::to_string(locator.slot) + " of "
				     + std::string(locator.rttiClass) + " did not resolve uniquely");
				continue;
			}
			const std::uint64_t target = RttiVtable::followAdjustorThunk(view, *slot);
			if (!starts.empty() && starts.count(target) == 0)
			{
				fail(name + ": slot target is not a function entry in .eh_frame");
				continue;
			}
			if (!locator.mapClass.empty())
			{
				const auto proxy = RttiVtable::slotFunction(
					view, locator.mapClass, "", static_cast<unsigned>(locator.slot));
				const auto names = proxy ? referencedNames(view, *proxy) : std::set<std::string>{};
				if (names.count(std::string(locator.methodName)) == 0)
				{
					fail(name + ": " + std::string(locator.mapClass) + " slot " + std::to_string(locator.slot)
					     + " does not reference '" + std::string(locator.methodName)
					     + "' -- the slot index no longer names this method");
					continue;
				}
			}
			if (!locator.evidence.empty())
			{
				const auto seen = referencedNames(view, *slot, 0x2000);
				const auto seenTarget = referencedNames(view, target, 0x2000);
				if (seen.count(std::string(locator.evidence)) == 0 && seenTarget.count(std::string(locator.evidence)) == 0)
				{
					fail(name + ": the resolved function does not reference '" + std::string(locator.evidence) + "'");
					continue;
				}
			}
			const auto inProcess = RttiVtable::slotFunction(
				loaded, locator.rttiClass, locator.rttiBase, static_cast<unsigned>(locator.slot));
			if (!inProcess || RttiVtable::followAdjustorThunk(loaded, *inProcess) != target)
			{
				fail(name + ": the relocated-memory view resolves differently from the file view");
				continue;
			}
			const auto whileOpening = RttiVtable::slotFunction(
				opening, locator.rttiClass, locator.rttiBase, static_cast<unsigned>(locator.slot));
			if (!whileOpening || RttiVtable::followAdjustorThunk(opening, *whileOpening) != target)
			{
				fail(name + ": the not-yet-relocated view resolves differently from the file view");
				continue;
			}
			std::cout << "ok   " << name << "  vtable " << locator.rttiClass << " slot " << locator.slot
			          << (target != *slot ? " (adjustor thunk followed)" : "") << "  target_rva=0x"
			          << std::hex << target << std::dec << '\n';
			continue;
		}

		const auto bytes = parsePattern(locator.pattern);
		if (bytes.empty())
		{
			fail(name + ": empty signature");
			continue;
		}

		struct Hit
		{
			const Segment* segment;
			const std::uint8_t* at;
		};
		std::vector<Hit> hits;
		for (const auto& segment : segments)
		{
			std::vector<uintptr_t> found;
			MemHlp::scanPatternRange(
				bytes, reinterpret_cast<uintptr_t>(segment.begin),
				reinterpret_cast<uintptr_t>(segment.end), true, &found,
				MemHlp::kMaxConvergenceCandidates);
			for (const uintptr_t address : found)
				hits.push_back({&segment, reinterpret_cast<const std::uint8_t*>(address)});
		}

		if (hits.empty())
		{
			fail(name + ": 0 matches in the module");
			continue;
		}

		std::set<std::uint64_t> targets;
		bool resolvable = true;
		for (const auto& hit : hits)
		{
			switch (locator.follow)
			{
				case PatternsX64::Follow::None:
					targets.insert(rva(*hit.segment, hit.at));
					break;
				case PatternsX64::Follow::Relative:
				{
					if (hit.at + 5 > hit.segment->end
					    || (hit.at[0] != 0xE8 && hit.at[0] != 0xE9))
					{
						resolvable = false;
						break;
					}
					std::int32_t displacement = 0;
					std::memcpy(&displacement, hit.at + 1, sizeof(displacement));
					targets.insert(rva(*hit.segment, hit.at) + 5
					               + static_cast<std::int64_t>(displacement));
					break;
				}
				case PatternsX64::Follow::StringXref:
				case PatternsX64::Follow::VtableSlot:
					resolvable = false; // handled before the signature scan
					break;
				case PatternsX64::Follow::PrologueUpwards:
				{
					std::optional<std::uint64_t> entry;
					const auto& prologue = locator.prologue;
					for (std::size_t i = 0; i < 0x10000 && !prologue.empty(); ++i)
					{
						if (static_cast<std::size_t>(hit.at - hit.segment->begin) < i + prologue.size() - 1)
							break;
						bool match = true;
						for (std::size_t j = 0; j < prologue.size(); ++j)
						{
							const int16_t expected = prologue[j];
							if (expected != -1 && expected != *(hit.at - i - j))
							{
								match = false;
								break;
							}
						}
						if (match)
						{
							entry = rva(*hit.segment, hit.at - i - prologue.size() + 1);
							break;
						}
					}
					if (!entry)
						resolvable = false;
					else
						targets.insert(*entry);
					break;
				}
			}
			if (!resolvable)
				break;
		}

		if (!resolvable)
		{
			fail(name + ": a match cannot be followed (" + std::to_string(hits.size()) + " match(es))");
			continue;
		}
		if (hits.size() > 1 && (locator.follow != PatternsX64::Follow::Relative || targets.size() != 1))
		{
			fail(name + ": " + std::to_string(hits.size()) + " matches that do not converge");
			continue;
		}
		if (targets.size() != 1)
		{
			fail(name + ": ambiguous target");
			continue;
		}

		if (locator.follow == PatternsX64::Follow::PrologueUpwards && !starts.empty()
		    && starts.count(*targets.begin()) == 0)
		{
			fail(name + ": prologue lands on 0x" + [&]{ std::ostringstream o; o << std::hex << *targets.begin(); return o.str(); }()
			     + ", which is not a function entry in .eh_frame");
			continue;
		}

		std::cout << "ok   " << name << "  matches=" << hits.size() << "  target_rva=0x"
		          << std::hex << *targets.begin() << std::dec << '\n';
	}

	if (failures != 0)
	{
		std::cerr << failures << " failure(s)\n";
		return 1;
	}
	if (!steamUi)
	{
		// Every VFTIndexes slot Moon hooks through a live interface vtable must still
		// name the intended method in the matching IClient*Map proxy.
		struct SlotCheck
		{
			std::string_view mapClass;
			int slot;
			std::string_view method;
		};
		const SlotCheck checks[] =
		{
			{ "14IClientAppsMap", VFTIndexes::IClientApps::GetAppData, "GetAppData" },
			{ "14IClientAppsMap", VFTIndexes::IClientApps::GetAppDataSection, "GetAppDataSection" },
			{ "14IClientAppsMap", VFTIndexes::IClientApps::RequestAppInfoUpdate, "RequestAppInfoUpdate" },
			{ "14IClientAppsMap", VFTIndexes::IClientApps::GetDLCCount, "GetDLCCount" },
			{ "14IClientAppsMap", VFTIndexes::IClientApps::GetDLCDataByIndex, "BGetDLCDataByIndex" },
			{ "14IClientAppsMap", VFTIndexes::IClientApps::GetAppType, "GetAppType" },
			{ "16IClientCompatMap", VFTIndexes::IClientCompat::SpecifyCompatTool, "SpecifyCompatTool" },
			{ "16IClientCompatMap", VFTIndexes::IClientCompat::GetCompatToolName, "GetCompatToolName" },
			{ "20IClientAppManagerMap", VFTIndexes::IClientAppManager::InstallApp, "InstallApp" },
			{ "20IClientAppManagerMap", VFTIndexes::IClientAppManager::UninstallApp, "UninstallApp" },
			{ "20IClientAppManagerMap", VFTIndexes::IClientAppManager::LaunchApp, "LaunchApp" },
			{ "20IClientAppManagerMap", VFTIndexes::IClientAppManager::GetAppInstallState, "GetAppInstallState" },
			{ "20IClientAppManagerMap", VFTIndexes::IClientAppManager::IsAppDlcInstalled, "IsAppDlcInstalled" },
			{ "20IClientAppManagerMap", VFTIndexes::IClientAppManager::BIsDlcEnabled, "BIsDlcEnabled" },
			{ "20IClientAppManagerMap", VFTIndexes::IClientAppManager::GetUpdateInfo, "GetUpdateInfo" },
			{ "23IClientRemoteStorageMap", VFTIndexes::IClientRemoteStorage::IsCloudEnabledForApp, "IsCloudEnabledForApp" },
			{ "14IClientUserMap", VFTIndexes::IClientUser::BLoggedOn, "BLoggedOn" },
			{ "14IClientUserMap", VFTIndexes::IClientUser::GetSteamID, "GetSteamID" },
			{ "14IClientUserMap", VFTIndexes::IClientUser::GetEncryptedAppTicket, "GetEncryptedAppTicket" },
			{ "15IClientUtilsMap", VFTIndexes::IClientUtils::GetOfflineMode, "GetOfflineMode" },
			{ "15IClientUtilsMap", VFTIndexes::IClientUtils::GetAppId, "GetAppID" },
		};
		for (const SlotCheck& check : checks)
		{
			const auto proxy = RttiVtable::slotFunction(view, check.mapClass, "", static_cast<unsigned>(check.slot));
			const auto names = proxy ? referencedNames(view, *proxy) : std::set<std::string>{};
			if (names.count(std::string(check.method)) == 0)
				fail("VFTIndexes: " + std::string(check.mapClass) + " slot " + std::to_string(check.slot)
				     + " does not name '" + std::string(check.method) + "'");
		}
		std::cout << sizeof(checks) / sizeof(checks[0]) << " VFTIndexes slots checked against the IClient*Map proxies\n";
	}

	std::cout << audited << " " << (steamUi ? "steamui" : "steamclient") << " x86_64 locators verified\n";
	return 0;
}

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		std::cerr << "usage: test_patterns_x86_64 <steamclient.so> <src/patterns.cpp> [steamui.so]\n";
		return 2;
	}
	int result = audit(argv[1], argv[2], false);
	if (argc >= 4)
		result |= audit(argv[3], argv[2], true);
	else
		std::cerr << "note: no steamui.so given, its locators are not audited\n";
	return result;
}
