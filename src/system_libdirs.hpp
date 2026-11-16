#pragma once

// Where the host's own shared libraries live, and how to pick one that this process can
// really load. Used for two things that reach processes other than Steam itself:
//  - the LD_LIBRARY_PATH suffix that src/main.cpp exports from the Steam process: every child
//    inherits it, including the file manager opened by "Browse local files" (this is the path
//    that applies in the 64-bit session as installed);
//  - the libcurl redirect of tools/library-inject, an LD_AUDIT library of every process it is
//    loaded into. The 64-bit wrapper does not put it in LD_AUDIT today (only the 32-bit build
//    needs it), but a leftover or older wrapper can, and then it runs in every child too.
//
// Both directions of the mistake are real: a 64-bit process handed /usr/lib32/libcurl.so.4
// (multilib Arch, issue 5), or a 32-bit one handed a 64-bit copy. So directories are tried in a
// fixed order AND each candidate is checked to be an ELF shared object of the process's own
// class and machine before it is returned. The class check is what makes a wrong directory
// harmless; the per-architecture list only keeps the lookups short. Fedora and Bazzite keep
// 32-bit libraries in /usr/lib, so a directory that is right on one distribution is wrong on
// another, which a list alone cannot express.

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <elf.h>
#include <fcntl.h>
#include <span>
#include <string>
#include <unistd.h>

namespace SystemLibDirs
{

#if defined(__x86_64__)
inline constexpr unsigned char kClass = ELFCLASS64;
inline constexpr std::uint16_t kMachine = EM_X86_64;
// 64-bit first, most specific first.
inline constexpr const char* kDirs[] = {
	"/usr/lib64",                // Fedora, Bazzite, openSUSE
	"/usr/lib/x86_64-linux-gnu", // Debian, Ubuntu, Mint, Pop!_OS
	"/usr/lib",                  // Arch, CachyOS, SteamOS (32-bit libraries on Fedora: skipped by the class check)
	"/usr/local/lib64",
	"/usr/local/lib",
};
#elif defined(__i386__)
inline constexpr unsigned char kClass = ELFCLASS32;
inline constexpr std::uint16_t kMachine = EM_386;
inline constexpr const char* kDirs[] = {
	"/usr/lib/i386-linux-gnu", // Debian, Ubuntu, Mint
	"/usr/lib32",              // Arch multilib, SteamOS
	"/usr/local/lib32",        // Bazzite
	"/usr/lib",                // 32-bit installs (64-bit libraries elsewhere: skipped by the class check)
	"/usr/local/lib",
};
#else
#error "SystemLibDirs: unsupported architecture"
#endif

// True when `path` (symlinks followed) starts with the header of a little-endian ELF shared
// object of the given class and machine. Reads 20 bytes with open/read only: this runs inside
// the dynamic linker's audit hook, so no stdio, no exceptions.
inline bool isLoadable(const char* path, unsigned char wantClass = kClass,
                       std::uint16_t wantMachine = kMachine) noexcept
{
	const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return false;
	unsigned char h[20];
	ssize_t n;
	do { n = ::read(fd, h, sizeof h); } while (n < 0 && errno == EINTR);
	::close(fd);
	if (n != static_cast<ssize_t>(sizeof h)) return false; // directory, empty or truncated file
	if (std::memcmp(h, ELFMAG, SELFMAG) != 0) return false;
	if (h[EI_CLASS] != wantClass || h[EI_DATA] != ELFDATA2LSB) return false;
	const std::uint16_t type = static_cast<std::uint16_t>(h[16] | (h[17] << 8));
	const std::uint16_t machine = static_cast<std::uint16_t>(h[18] | (h[19] << 8));
	return type == ET_DYN && machine == wantMachine;
}

// The first directory, in the given order, that holds `name` as a library this process can
// load; empty when there is none. A directory that is missing, or holds only a library of the
// other class, is skipped, never an error.
inline std::string find(const char* name, std::span<const char* const> dirs,
                        unsigned char wantClass = kClass,
                        std::uint16_t wantMachine = kMachine)
{
	for (const char* dir : dirs)
	{
		std::string path(dir);
		path.push_back('/');
		path.append(name);
		if (isLoadable(path.c_str(), wantClass, wantMachine)) return path;
	}
	return {};
}

// ":dir:dir..." to append to LD_LIBRARY_PATH, in the same order and from the same list as the
// redirect above, so that a redirected libcurl finds its own dependencies where it was found.
inline std::string ldLibraryPathSuffix()
{
	std::string out;
	for (const char* dir : kDirs)
	{
		out.push_back(':');
		out.append(dir);
	}
	return out;
}

} // namespace SystemLibDirs
