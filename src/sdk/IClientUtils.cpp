#include "IClientUtils.hpp"

#include "../hooks.hpp"
#include "../patterns.hpp"

#include "libmem/libmem.h"
#include "steam.hpp"


HSteamPipe IClientUtils::getCurrentSteamPipe()
{
	//Offset found in IClientUtils::GetAppId
	// `mov esi,[rdi+disp32]` / `mov edx,[ecx+disp32]`: a 4-byte displacement, not a
	// pointer-sized one.
	const static auto offset = *reinterpret_cast<int32_t*>(Patterns::IClientUtils::Offset_GetPipeIndex.address + 0x2);
	return *reinterpret_cast<HSteamPipe*>(this + offset);
}


uint32_t IClientUtils::getAppId()
{
	return Hooks::IClientUtils_GetAppId.originalFn.fn(this);
}

IClientUtils* g_pClientUtils;
