#include "../src/filewatcher_burst.hpp"

#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;

namespace
{
int failures = 0;

#define CHECK(condition, message) do {                                      \
	if (!(condition)) { std::printf("FAIL: %s\n", message); ++failures; }  \
	else              { std::printf("ok:   %s\n", message); }              \
} while (0)

void quietWindowCoalescesOneBurst()
{
	int pipeFds[2]{};
	CHECK(pipe(pipeFds) == 0, "burst fixture pipe created");
	const char first = 'a';
	CHECK(write(pipeFds[1], &first, 1) == 1, "initial readable byte queued");

	char byte = 0;
	CHECK(read(pipeFds[0], &byte, 1) == 1, "initial readable byte consumed");
	std::thread writer([&] {
		for (int i = 0; i < 5; ++i)
		{
			std::this_thread::sleep_for(15ms);
			const char next = static_cast<char>('b' + i);
			(void)write(pipeFds[1], &next, 1);
		}
	});

	const auto start = std::chrono::steady_clock::now();
	const auto drained = FileWatcherBurst::drainAdditionalReadable(
		pipeFds[0], /*quietWindowMs=*/40, /*maxWindowMs=*/500);
	const auto elapsed = std::chrono::steady_clock::now() - start;
	writer.join();
	close(pipeFds[0]);
	close(pipeFds[1]);

	CHECK(drained == 5, "all writes in one burst are drained together");
	CHECK(elapsed >= 90ms, "quiet window restarts after each arriving write");
}

void maximumWindowBoundsContinuousTraffic()
{
	int pipeFds[2]{};
	CHECK(pipe(pipeFds) == 0, "continuous fixture pipe created");
	const char first = 'a';
	CHECK(write(pipeFds[1], &first, 1) == 1, "continuous initial byte queued");
	char byte = 0;
	CHECK(read(pipeFds[0], &byte, 1) == 1, "continuous initial byte consumed");

	std::thread writer([&] {
		for (int i = 0; i < 30; ++i)
		{
			std::this_thread::sleep_for(10ms);
			(void)write(pipeFds[1], &first, 1);
		}
	});

	const auto start = std::chrono::steady_clock::now();
	const auto drained = FileWatcherBurst::drainAdditionalReadable(
		pipeFds[0], /*quietWindowMs=*/50, /*maxWindowMs=*/100);
	const auto elapsed = std::chrono::steady_clock::now() - start;
	writer.join();
	close(pipeFds[0]);
	close(pipeFds[1]);

	CHECK(drained > 0 && drained < 30,
	      "bounded coalescing returns before continuous traffic ends");
	CHECK(elapsed >= 80ms && elapsed < 220ms,
	      "continuous traffic is bounded by the maximum window");
}

// The steamrt64 bootstrapper closes every inherited descriptor after the config watcher has
// started. poll() then reports POLLNVAL on every call without waiting; the loop used to treat
// that as a timeout and spun a core. waitReadable must say Closed, at once, instead.
void waitReadableTellsClosedFromIdle()
{
	using FileWatcherBurst::Wait;
	using FileWatcherBurst::waitReadable;
	int p[2]{};
	CHECK(pipe(p) == 0, "wait fixture pipe created");

	auto start = std::chrono::steady_clock::now();
	CHECK(waitReadable(p[0], 40) == Wait::Idle, "an open descriptor with nothing to read is Idle");
	CHECK(std::chrono::steady_clock::now() - start >= 30ms, "Idle waits for the timeout");

	const char c = 'x';
	CHECK(write(p[1], &c, 1) == 1, "byte queued");
	CHECK(waitReadable(p[0], 40) == Wait::Readable, "pending data is Readable");

	// A final batch must still be read even if the writer is already gone.
	close(p[1]);
	CHECK(waitReadable(p[0], 40) == Wait::Readable, "pending data wins over a hung-up writer");
	char byte = 0;
	CHECK(read(p[0], &byte, 1) == 1, "byte consumed");

	start = std::chrono::steady_clock::now();
	CHECK(waitReadable(p[0], 2000) == Wait::Closed, "a hung-up descriptor with nothing left is Closed");
	CHECK(std::chrono::steady_clock::now() - start < 500ms, "Closed is reported at once, not after the timeout");
	close(p[0]);

	const int gone = open("/dev/null", O_RDONLY);
	CHECK(gone >= 0, "descriptor opened");
	close(gone);
	start = std::chrono::steady_clock::now();
	CHECK(waitReadable(gone, 2000) == Wait::Closed, "a closed descriptor (POLLNVAL) is Closed");
	CHECK(std::chrono::steady_clock::now() - start < 500ms, "and reported at once");
	CHECK(waitReadable(-1, 40) == Wait::Idle || waitReadable(-1, 40) == Wait::Closed,
	      "a negative descriptor is never Readable");
}
} // namespace

int main()
{
	quietWindowCoalescesOneBurst();
	maximumWindowBoundsContinuousTraffic();
	waitReadableTellsClosedFromIdle();
	if (failures == 0) std::puts("all filewatcher burst checks passed");
	return failures == 0 ? 0 : 1;
}
