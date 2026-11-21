#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <link.h>
#include <string>
#include <unordered_set>

#include "system_libdirs.hpp"


//std::ofstream logFile("/tmp/env-mod.log");

static std::unordered_set<std::string> LIBS =
{
	"libcurl.so",
	"libcurl.so.3",
	"libcurl.so.4",
	"libcurl.so.4.0.0",
	"libcurl.so.4.1.0",
	"libcurl.so.4.2.0",
	"libcurl.so.4.3.0",
	"libcurl.so.4.4.0",
	"libcurl.so.4.5.0",
	"libcurl.so.4.6.0",
	"libcurl.so.4.7.0",
	"libcurl.so.4.8.0",
};

char *la_objsearch(const char *name, __attribute__((unused)) uintptr_t *cookie, unsigned int flag)
{
	if (flag != LA_SER_ORIG)
	{
		return const_cast<char*>(name);
	}

	//logFile << "objopen: " << name << std::endl;

	if(LIBS.contains(name))
	{
		//logFile << "Replacing: " << name << std::endl;

		// A library of the wrong class would make the process fail to start it: see
		// SystemLibDirs::find, which only returns one this process can load.
		const std::string found = SystemLibDirs::find(name, SystemLibDirs::kDirs);
		if (!found.empty())
		{
			if (char* n = static_cast<char*>(malloc(found.size() + 1)))
			{
				memcpy(n, found.c_str(), found.size() + 1);
				return n;
			}
		}
	}

	return const_cast<char*>(name);
}

unsigned int la_version(unsigned int)
{
	return LAV_CURRENT;
}

//void la_preinit(__attribute__((unused)) uintptr_t *cookie)
//{
//}
