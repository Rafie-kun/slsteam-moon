/* Controlled load test: map the 64-bit Steam client modules into a throw-away process
 * with SLSsteam.so as LD_AUDIT, so la_objopen -> load() -> Patterns::init() ->
 * Hooks::setup() run against the real modules without starting Steam (no login, no
 * network, no pipe to a running client).  The modules' own static initialisers run
 * (CreateInterface is never called).  Exits with _exit() so no library destructor runs.
 *
 *   load_client <steamclient.so> <steamui.so> [settle-seconds]
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void *open_module(const char *path)
{
	void *handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
	if (!handle)
		fprintf(stderr, "load_client: dlopen(%s) failed: %s\n", path, dlerror());
	else
		fprintf(stderr, "load_client: opened %s\n", path);
	return handle;
}

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		fprintf(stderr, "usage: %s <steamclient.so> <steamui.so> [settle-seconds]\n", argv[0]);
		return 2;
	}
	const unsigned settle = argc > 3 ? (unsigned)atoi(argv[3]) : 2;

	if (!open_module(argv[1]))
		return 1;
	if (!open_module(argv[2]))
		return 1;

	/* Let anything the load path started finish its first pass. */
	sleep(settle);
	fprintf(stderr, "load_client: survived %us after both modules were opened\n", settle);
	fflush(stderr);
	_exit(0);
}
