// Regression test for the libcurl redirect (issue 5): a process must never be handed a system
// library of the other ELF class, and the directories are tried in a fixed order.
//
// Usage: test_library_inject <path to bin/library-inject.so>

#include "system_libdirs.hpp"

#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{
	int failures = 0;

	void check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}

	std::string root;

	std::string dir(const char* name)
	{
		const std::string path = root + "/" + name;
		mkdir(path.c_str(), 0700);
		return path;
	}

	// 20 bytes are all the check reads: magic, class, data, type, machine.
	std::vector<unsigned char> header(unsigned char cls, std::uint16_t machine,
	                                  std::uint16_t type = ET_DYN,
	                                  unsigned char data = ELFDATA2LSB)
	{
		std::vector<unsigned char> h(64, 0);
		h[0] = 0x7f; h[1] = 'E'; h[2] = 'L'; h[3] = 'F';
		h[EI_CLASS] = cls; h[EI_DATA] = data; h[EI_VERSION] = 1;
		h[16] = type & 0xff; h[17] = type >> 8;
		h[18] = machine & 0xff; h[19] = machine >> 8;
		return h;
	}

	void put(const std::string& path, const std::vector<unsigned char>& bytes)
	{
		const int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
		if (fd < 0) { std::perror(path.c_str()); std::exit(2); }
		if (write(fd, bytes.data(), bytes.size()) < 0) std::perror("write");
		close(fd);
	}

	const std::vector<unsigned char> lib64 = header(ELFCLASS64, EM_X86_64);
	const std::vector<unsigned char> lib32 = header(ELFCLASS32, EM_386);

	std::string find64(std::initializer_list<const char*> dirs)
	{
		const std::vector<const char*> v(dirs);
		return SystemLibDirs::find("libcurl.so.4", v, ELFCLASS64, EM_X86_64);
	}
}

int main(int argc, char** argv)
{
	char tmpl[] = "/tmp/slsm-libinject-XXXXXX";
	if (!mkdtemp(tmpl)) { std::perror("mkdtemp"); return 2; }
	root = tmpl;

	const std::string d32 = dir("lib32"), d64 = dir("lib64"), dBoth = dir("other"),
	                  dText = dir("text"), dMissing = root + "/absent";
	put(d32 + "/libcurl.so.4", lib32);
	put(d64 + "/libcurl.so.4", lib64);
	put(dBoth + "/libcurl.so.4", lib64);

	// The reported case: the 32-bit directory comes first and holds a copy.
	check(find64({d32.c_str(), d64.c_str()}) == d64 + "/libcurl.so.4",
	      "a 32-bit copy in an earlier directory is skipped for a 64-bit process");
	check(find64({d32.c_str()}).empty(), "only a 32-bit copy: nothing is returned");
	check(find64({dMissing.c_str(), d64.c_str()}) == d64 + "/libcurl.so.4",
	      "a missing directory is skipped");
	check(find64({d64.c_str(), dBoth.c_str()}) == d64 + "/libcurl.so.4",
	      "with two valid copies the first directory wins (fixed order)");
	check(find64({dBoth.c_str(), d64.c_str()}) == dBoth + "/libcurl.so.4",
	      "and the order is the one given");
	check(SystemLibDirs::find("libcurl.so.4", std::vector<const char*>{d32.c_str(), d64.c_str()},
	                          ELFCLASS32, EM_386) == d32 + "/libcurl.so.4",
	      "a 32-bit process gets the 32-bit copy");

	// Things that are not a loadable library of the right kind.
	put(dText + "/libcurl.so.4", std::vector<unsigned char>(64, 'x'));
	check(find64({dText.c_str()}).empty(), "a text file is rejected");
	put(dText + "/libcurl.so.4", std::vector<unsigned char>(lib64.begin(), lib64.begin() + 10));
	check(find64({dText.c_str()}).empty(), "a truncated header is rejected");
	put(dText + "/libcurl.so.4", header(ELFCLASS64, EM_X86_64, ET_EXEC));
	check(find64({dText.c_str()}).empty(), "an executable (not a shared object) is rejected");
	put(dText + "/libcurl.so.4", header(ELFCLASS64, EM_X86_64, ET_DYN, ELFDATA2MSB));
	check(find64({dText.c_str()}).empty(), "a big-endian object is rejected");
	put(dText + "/libcurl.so.4", header(ELFCLASS64, EM_AARCH64));
	check(find64({dText.c_str()}).empty(), "another machine is rejected");
	unlink((dText + "/libcurl.so.4").c_str());
	mkdir((dText + "/libcurl.so.4").c_str(), 0700);
	check(find64({dText.c_str()}).empty(), "a directory named like the library is rejected");
	rmdir((dText + "/libcurl.so.4").c_str());
	if (symlink((d64 + "/libcurl.so.4").c_str(), (dText + "/libcurl.so.4").c_str()) == 0)
		check(find64({dText.c_str()}) == dText + "/libcurl.so.4", "a symlink to a valid library is accepted");

	// The list itself: this build must not look in the other class's directories first.
#if defined(__x86_64__)
	for (const char* d : SystemLibDirs::kDirs)
		check(std::string(d).find("lib32") == std::string::npos
		      && std::string(d).find("i386") == std::string::npos,
		      "no 32-bit directory in the x86_64 list");
	check(std::string(SystemLibDirs::ldLibraryPathSuffix()).find("lib32") == std::string::npos,
	      "no lib32 in the LD_LIBRARY_PATH suffix");
#endif
	check(SystemLibDirs::ldLibraryPathSuffix().front() == ':', "the suffix starts with ':'");

	// The real library, loaded the way the dynamic linker loads an auditor.
	if (argc > 1)
	{
		void* h = dlopen(argv[1], RTLD_NOW);
		check(h != nullptr, "bin/library-inject.so loads");
		if (h)
		{
			using objsearch_t = char* (*)(const char*, uintptr_t*, unsigned int);
			const auto fn = reinterpret_cast<objsearch_t>(dlsym(h, "la_objsearch"));
			check(fn != nullptr, "la_objsearch is exported");
			if (fn)
			{
				const char* name = "libcurl.so.4";
				char* r = fn(name, nullptr, LA_SER_ORIG);
				check(r == name || SystemLibDirs::isLoadable(r),
				      "the redirect returns nothing or a library this process can load");
				if (r != name) std::printf("redirect: %s -> %s\n", name, r);
				check(fn("libfoo.so.1", nullptr, LA_SER_ORIG) != nullptr
				      && std::string(fn("libfoo.so.1", nullptr, LA_SER_ORIG)) == "libfoo.so.1",
				      "another library name is left alone");
				check(fn(name, nullptr, LA_SER_DEFAULT) == name, "a non-original search is left alone");
			}
			dlclose(h);
		}
	}

	const std::string cleanup = "rm -rf '" + root + "'";
	if (std::system(cleanup.c_str()) != 0) std::perror("cleanup");
	if (failures) { std::fprintf(stderr, "test_library_inject: %d failure(s)\n", failures); return 1; }
	std::puts("library inject tests passed");
	return 0;
}
