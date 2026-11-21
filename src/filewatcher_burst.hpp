#pragma once

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <poll.h>
#include <unistd.h>

namespace FileWatcherBurst
{

enum class Wait
{
	Readable, // POLLIN: an inotify batch is waiting
	Idle,     // timeout or EINTR: nothing to read, go round again
	Closed    // the descriptor can never deliver another event: stop watching
};

// One bounded wait on the watcher's inotify descriptor.
//
// POLLNVAL (descriptor closed), POLLERR and POLLHUP are level-triggered: poll() reports
// them immediately on every call. Treating them like a timeout turns the bounded wait into
// a busy loop that burns a core for the whole session. That is what happened in the
// steamrt64 bootstrapper, which closes every inherited descriptor after the library had
// already started its watcher. Pending data is reported first so a final batch is still read.
inline Wait waitReadable(int fd, int timeoutMs) noexcept
{
	struct pollfd pfd{};
	pfd.fd = fd;
	pfd.events = POLLIN;
	const int ready = poll(&pfd, 1, timeoutMs);
	if (ready < 0) return errno == EINTR ? Wait::Idle : Wait::Closed;
	if (ready == 0) return Wait::Idle;
	if ((pfd.revents & POLLIN) != 0) return Wait::Readable;
	if ((pfd.revents & (POLLNVAL | POLLERR | POLLHUP)) != 0) return Wait::Closed;
	return Wait::Idle;
}

// The caller has already consumed the first readable batch. Drain every
// additional batch that arrives inside one quiet window, but return
// periodically under continuous traffic so configuration updates cannot be
// postponed forever. This turns a bulk directory copy into one logical reload
// instead of one reload per 4 KiB inotify read.
inline std::size_t drainAdditionalReadable(
	int fd, int quietWindowMs = 250, int maxWindowMs = 5000) noexcept
{
	if (fd < 0 || quietWindowMs <= 0 || maxWindowMs <= 0) return 0;

	using Clock = std::chrono::steady_clock;
	const auto started = Clock::now();
	std::size_t total = 0;
	alignas(std::max_align_t) char buffer[4096];

	for (;;)
	{
		const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			Clock::now() - started).count();
		const int remainingMs = maxWindowMs - static_cast<int>(elapsedMs);
		if (remainingMs <= 0) return total;

		struct pollfd pfd{};
		pfd.fd = fd;
		pfd.events = POLLIN;
		int ready = 0;
		do
		{
			ready = poll(&pfd, 1, std::min(quietWindowMs, remainingMs));
		} while (ready < 0 && errno == EINTR);

		if (ready <= 0 || (pfd.revents & POLLIN) == 0) return total;

		ssize_t bytes = 0;
		do
		{
			bytes = read(fd, buffer, sizeof(buffer));
		} while (bytes < 0 && errno == EINTR);
		if (bytes <= 0) return total;
		total += static_cast<std::size_t>(bytes);
	}
}

} // namespace FileWatcherBurst
