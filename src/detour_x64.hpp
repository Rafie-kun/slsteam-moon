#pragma once

// x86_64 inline detours with a trampoline that is really executable.
//
// LM_HookCode copies the displaced prologue verbatim into memory it allocates anywhere in
// the address space.  On x86_64 that breaks every displaced instruction that is relative to
// RIP or to its own address (`mov rax,[rip+d]`, `call rel32`, `jcc rel8`...): they land on the
// wrong target, or cannot be re-encoded at all once the trampoline is more than 2 GiB from
// the original (an observed distance was 5 GiB).  This builder instead
//   - allocates one page within +-2 GiB of the target (MAP_FIXED_NOREPLACE probing);
//   - relocates each displaced instruction by patching its displacement for its new address
//     (rel8 jumps are widened to rel32);
//   - patches the target with a 5-byte `jmp rel32` to an island next to the trampoline, which
//     holds the 14-byte absolute jump to the hook (so the hook may be anywhere).
// It fails (returning nothing, writing nothing) when it cannot do all of that.

#include "libmem/libmem.h"

#include <cstddef>
#include <optional>
#include <string>

namespace DetourX64
{
	struct Placed
	{
		lm_address_t target = 0;
		lm_address_t trampoline = 0;
		std::size_t displaced = 0; // bytes of the target overwritten (>= 5)
	};

	// `error` receives the reason when nothing was placed.
	std::optional<Placed> place(lm_address_t target, lm_address_t hook, std::string* error = nullptr);

	// Restores the original bytes.  The trampoline page is intentionally left mapped: another
	// thread may still be executing in it.
	bool remove(lm_address_t target);
}
