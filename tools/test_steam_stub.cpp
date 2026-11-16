// Stand-in for the steamrt64 `steam` process, copied under that name by
// scripts/test-bootstrapper-x64.sh and run with the library in LD_AUDIT.
//
//   steam env             print LD_AUDIT (it must reach the client that is launched next)
//   steam spin [seconds]  like the bootstrapper: close every inherited descriptor, then wait
//                         and report the CPU time the whole process used while waiting
//
// A process that is only waiting must use almost no CPU. A thread that polls a closed
// descriptor in a loop does not: it burns a full core.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/resource.h>
#include <unistd.h>

static long cpuMs()
{
	rusage ru{};
	getrusage(RUSAGE_SELF, &ru);
	return (ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000L
	     + (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1000L;
}

int main(int argc, char** argv)
{
	const char* mode = argc > 1 ? argv[1] : "env";
	if (std::strcmp(mode, "env") == 0)
	{
		const char* audit = std::getenv("LD_AUDIT");
		std::puts(audit ? audit : "");
		return 0;
	}
	if (std::strcmp(mode, "spin") == 0)
	{
		const double seconds = argc > 2 ? std::atof(argv[2]) : 2.5;
		sleep(1); // setup() already ran in the loader's preinit; let any thread it started settle
		for (int fd = 3; fd < 1024; ++fd) close(fd);
		const long before = cpuMs();
		usleep(static_cast<useconds_t>(seconds * 1e6));
		std::printf("cpu_ms=%ld\n", cpuMs() - before);
		return 0;
	}
	std::fprintf(stderr, "usage: steam env | steam spin [seconds]\n");
	return 2;
}
