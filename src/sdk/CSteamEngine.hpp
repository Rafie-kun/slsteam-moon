#pragma once

#include "CUtl.hpp"
#include "steam.hpp"

#include <cstddef>
#include <cstdint>

class CUser;
class IClientCompat;
class IClientUtils;

class IProcessPipe { };
using CUtlString = char*;

// Steam has shipped more than one ABI for this object, and the fork only ever
// reads two fields (pipeHandle, pid), so the 64-bit layout is kept to the same
// two fields plus the leading pointers it has to skip.
//
// The 64-bit offsets are AceSLS/SLSsteam's (src/sdk/CSteamEngine.hpp, reverse
// engineered against a shipped x86_64 client).  They were not re-verified
// against the steamrt64 build this fork ships for, so consumers treat a
// non-plausible pid as "layout unknown" instead of dereferencing on trust; see
// plausibleServerPipe().
class __attribute__((packed)) __attribute__((aligned(1))) CServerPipe
{
public:
#if defined(__x86_64__)
	IProcessPipe* internalPipe;		//0x0
	IProcessPipe* singleProcessPipe;	//0x8
	HSteamPipe pipeHandle;				//0x10
	uint8_t __pad0x14[0xC];				//0x14
	int32_t pid;						//0x20
	int32_t threadId;					//0x24
	CUtlString processName;				//0x28
#else
	IProcessPipe* internalPipe;		//0x0
	IProcessPipe* singleProcessPipe;	//0x4
	HSteamPipe pipeHandle;				//0x8
	uint8_t __pad0xC[0x8];				//0xC
	int32_t pid;						//0x14
	int32_t threadId;					//0x18
	CUtlString processName;				//0x1C - Was empty on the stuff I tried, maybe it's defunct on linux?
	uint8_t __pad0x20[0x1];				//0x20
	HSteamUser userHandle;					//0x21
	uint8_t __pad0x25[0x7];				//0x25
	void* queueCallbackMsg;				//0x2C
	uint8_t __pad0x30[0x8];				//0x30
	uint32_t numQueuedCallbacks;		//0x38
	uint8_t __pad0x3C[0x14];			//0x3C
	CUtlVector<void> debugText;			//0x50
#endif
}; //0x60 (32-bit) / 0x30 (64-bit)

#if defined(__x86_64__)
static_assert(offsetof(CServerPipe, pipeHandle) == 0x10);
static_assert(offsetof(CServerPipe, pid) == 0x20);
static_assert(sizeof(CServerPipe) == 0x30);
#else
static_assert(offsetof(CServerPipe, pipeHandle) == 0x8);
static_assert(offsetof(CServerPipe, pid) == 0x14);
#endif

// A pid outside this range means the 64-bit layout guess is wrong: the 32-bit
// offsets were measured on a live client, the 64-bit ones were not, and reading
// the wrong field would key the process map off whatever integer sits at 0x20.
inline bool plausibleServerPipe(const CServerPipe* const pipe)
{
	if (pipe == nullptr)
	{
		return false;
	}
	return pipe->pid > 0 && pipe->pid < (1 << 22);
}

class CSteamEngine
{
public:
	CServerPipe* getServerPipe(HSteamPipe pipe);
	CUser* getUser(uint32_t index);
	IClientUtils* getUtils();
	void setAppIdForCurrentPipe(uint32_t appId);
};

extern CSteamEngine* g_pSteamEngine;

// Fallback to the local CUser* captured from the CUser::CheckAppOwnership
// hook.  Needed because CSteamEngine::Init is a one-shot call that Steam
// makes during early bootstrap; under the LD_PRELOAD injection model our
// hooks are placed after that call already ran, so g_pSteamEngine can stay
// null.  CheckAppOwnership fires early and often with the same CUser* that
// getUser(0) would return, so it is a reliable, timing-independent source.
extern CUser* g_pLocalUser;

// Returns the local user (pipe 0), preferring the engine when available and
// falling back to the cached CheckAppOwnership user.  May return nullptr if
// neither source has been observed yet; callers MUST null-check.
CUser* getLocalUser();

// Resolve CCompatManager, which implements IClientCompat and is embedded in
// the local CUser. Returns null when the optional member locator drifted or
// the local user has not been observed yet.
IClientCompat* getLocalClientCompat();
