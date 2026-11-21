#pragma once

// Locators for the Linux x86_64 (steamrt64) steamclient.so.
//
// The i386 table in patterns.cpp must never be scanned in a 64-bit module:
// a short i386 byte string can match unrelated x86_64 code and the detour would
// land mid-function.  On x86_64 only the names listed here are scanned; every
// other locator stays unresolved, which a required hook reports as a failed
// load instead of a wrong hook.
//
// Each entry is re-verified offline against the 64-bit client the Steam process
// loads, steamrt64/steamclient.so (make test-patterns-x86_64): exactly one
// match, or several relative call sites converging on one target.  NOTE: that
// is steamrt64/, not linux64/steamclient.so, which is a different binary.
//
// Matching the signature only proves WHERE the function is.  The hook's
// argument list and the SDK structs it touches are validated separately.

#include <cstdint>
#include <string_view>
#include <vector>

namespace PatternsX64
{
	enum class Follow
	{
		None,
		Relative,
		PrologueUpwards,
		// Not a byte signature: slot `slot` of the vtable that `rttiClass` uses
		// for `rttiBase` (see rtti_vtable.hpp), following an adjustor thunk.
		VtableSlot,
		// Not a byte signature: the one function that loads the address of every
		// string in `xrefStrings` (log text and VPROF names are stable across
		// builds).  Entry from .eh_frame_hdr.
		StringXref,
	};

	struct Locator
	{
		std::string_view name;      // Pattern_t::name in patterns.cpp
		std::string_view pattern;
		Follow follow;
		std::vector<int16_t> prologue;

		// VtableSlot only.
		std::string_view rttiClass = {};   // mangled name of the implementing class
		std::string_view rttiBase = {};    // mangled name of the interface base, empty = primary vtable
		int slot = -1;
		// Offline audit only: the IClient*Map proxy class whose slot `slot` must
		// reference the string `methodName`.  This proves the index still names
		// the intended method, which a bare slot number cannot.
		std::string_view mapClass = {};
		std::string_view methodName = {};
		// Which module the locator lives in: steamclient.so (default) or steamui.so.
		bool steamUi = false;
		// Offline audit only: a string the resolved function must reference (a
		// VPROF scope or log text naming it), for slots no IClient*Map covers.
		std::string_view evidence = {};
		// StringXref only.
		std::vector<std::string_view> xrefStrings = {};
		// StringXref only: break a tie between several referencing functions by
		// direct-call count (see RttiVtable::functionReferencingAll).
		bool preferMostCalled = false;
		// StringXref only: the opposite tie-break, for a function whose twin (same
		// strings) is the one with the many call sites.
		bool preferFewestCalled = false;
		// false = located and audited offline, but not scanned in the process: its
		// hook is not ported to the 64-bit layouts yet, so resolving it would install
		// an i386-shaped hook on a 64-bit function.
		bool bindAtRuntime = true;
	};

	inline const std::vector<Locator>& locators()
	{
		// Prologue bytes are listed in the order PrologueUpwards reads them,
		// i.e. walking backwards from the match: {55 41 56 41 57 41} is
		// `41 57 41 56 41 55` in memory (push r15; push r14; push r13).
		static const std::vector<Locator> table =
		{
			{ "TraceIPC",
			  "48 89 D7 C1 E8",
			  Follow::PrologueUpwards, { 0x55, 0x41, 0x56, 0x41, 0x57, 0x41 } },
			{ "CSteamEngine::GetServerPipe",
			  "0F B7 CE 31 D2",
			  Follow::PrologueUpwards, { 0x2C, 0x74, 0xF6, 0x85 } },
			{ "CSteamEngine::SetAppIdForCurrentPipe",
			  "0F 43 C6 83 FB",
			  Follow::PrologueUpwards, { 0x55, 0x41, 0x56, 0x41, 0x57, 0x41 } },
			{ "CSteamEngine::ProcessIPCFrame",
			  "48 89 DA 44 89 E6 5B",
			  Follow::PrologueUpwards, { 0xCD, 0x89, 0x49, 0x55, 0x41 } },
			{ "CSteamEngine::m_pUser",
			  "4C 03 A5 ? ? ? ? 48 89 DF 4C 89 E2 E8 ? ? ? ? 49 8B 74 24",
			  Follow::None, {} },
			{ "CUser::CheckAppOwnership",
			  "48 89 E8 31 ED",
			  Follow::PrologueUpwards, { 0xD6, 0x89, 0x49, 0x56, 0x41, 0x57, 0x41 } },
			{ "CUser::PostCallbackToAppId",
			  "44 3B 69 ? 41 89 C5",
			  Follow::PrologueUpwards, { 0x55, 0x41, 0x56, 0x41, 0x57, 0x41 } },
			{ "IClientUtils::m_PipeIndex",
			  "8B B7 ? ? ? ? 64 48 8B 1C 25 ? ? ? ? 48 89 5C 24 ? 48 89 FB 48 81 EF",
			  Follow::None, {} },
			// IPC interface dispatchers: `call <read hash>; mov eax,[rbp-x]; cmp eax,ROOT`
			// at the head of a function that CSteamEngine's interface switch
			// calls on the embedded interface object.  ROOT is the median of the
			// interface's function-hash tree: it drifts a little between client
			// builds and is re-derived at load by autoResolveIpcFrameRoots (seed
			// within 0x10000, or the high-24 pivot fingerprint).  The trailing
			// four bytes MUST stay the root, as for the i386 table.
			{ "IClientAppManager::RunIPCFrame",
			  "E8 ? ? ? ? 8B 85 ? ? ? ? 3D B4 85 0A 7A",
			  Follow::PrologueUpwards, { 0xE5, 0x89, 0x48, 0x55 } },
			{ "IClientApps::RunIPCFrame",
			  "E8 ? ? ? ? 8B 85 ? ? ? ? 3D 33 9C 88 A6",
			  Follow::PrologueUpwards, { 0xE5, 0x89, 0x48, 0x55 } },
			{ "IClientRemoteStorage::RunIPCFrame",
			  "E8 ? ? ? ? 8B 85 ? ? ? ? 3D 5C DD 12 87",
			  Follow::PrologueUpwards, { 0xE5, 0x89, 0x48, 0x55 } },
			{ "IClientUser::RunIPCFrame",
			  "E8 ? ? ? ? 8B 85 ? ? ? ? 3D 83 63 5B 73",
			  Follow::PrologueUpwards, { 0xE5, 0x89, 0x48, 0x55 } },
			{ "IClientUGC::RunIPCFrame",
			  "E8 ? ? ? ? 8B 85 ? ? ? ? 3D 41 0C D2 71",
			  Follow::PrologueUpwards, { 0xE5, 0x89, 0x48, 0x55 } },
			{ "IClientUserStats::RunIPCFrame",
			  "E8 ? ? ? ? 8B 85 ? ? ? ? 3D 62 D6 E5 85",
			  Follow::PrologueUpwards, { 0xE5, 0x89, 0x48, 0x55 } },
			// Same shape as the others on x86_64 (the i386 Utils dispatcher had a
			// different one).  CSteamEngine passes `this+0x1118` -- its IClientUtils
			// base -- to this function from its interface switch.
			{ "IClientUtils::RunIPCFrame",
			  "E8 ? ? ? ? 8B 85 ? ? ? ? 3D 3B EB 7E 7F",
			  Follow::PrologueUpwards, { 0xE5, 0x89, 0x48, 0x55 } },
			// Virtual methods, found through RTTI.  Slot numbers are the method order
			// of the interface, identical on i386 and x86_64 (checked against the
			// IClient*Map proxies by the audit).  IClientUser lives in CUser at a
			// secondary-base offset; its adjustor thunks are followed to the real
			// function, as the i386 signatures did.
			{ "IClientUser::BLoggedOn", "", Follow::VtableSlot, {},
			  "5CUser", "11IClientUser", 4, "14IClientUserMap", "BLoggedOn" },
			{ "IClientUser::BUpdateAppOwnershipTicket", "", Follow::VtableSlot, {},
			  "5CUser", "11IClientUser", 69, "14IClientUserMap", "BUpdateAppOwnershipTicket" },
			{ "IClientUser::RequiresLegacyCDKey", "", Follow::VtableSlot, {},
			  "5CUser", "11IClientUser", 87, "14IClientUserMap", "RequiresLegacyCDKey" },
			{ "IClientUser::GetAppOwnershipTicketExtendedData", "", Follow::VtableSlot, {},
			  "5CUser", "11IClientUser", 106, "14IClientUserMap", "GetAppOwnershipTicketExtendedData" },
			{ "IClientUser::IsUserSubscribedAppInTicket", "", Follow::VtableSlot, {},
			  "5CUser", "11IClientUser", 119, "14IClientUserMap", "IsUserSubscribedAppInTicket" },
			{ "CUser::GetSubscribedApps", "", Follow::VtableSlot, {},
			  "5CUser", "11IClientUser", 183, "14IClientUserMap", "GetSubscribedApps" },
			{ "IClientAppManager::BCanRemotePlayTogether", "", Follow::VtableSlot, {},
			  "15CUserAppManager", "", 73, "20IClientAppManagerMap", "BCanRemotePlayTogether" },
			{ "IClientFriends::GetFriendGamePlayed", "", Follow::VtableSlot, {},
			  "12CUserFriends", "14IClientFriends", 23, "17IClientFriendsMap", "GetFriendGamePlayed" },
			// ISteamMatchmakingServers order: RequestInternetServerList = 0, GetServerDetails = 7.
			{ "CSteamMatchmakingServers::RequestInternetServerList", "", Follow::VtableSlot, {},
			  "24CSteamMatchMakingServers", "", 0 },
			{ "CSteamMatchmakingServers::GetServerDetails", "", Follow::VtableSlot, {},
			  "24CSteamMatchMakingServers", "", 7 },
			// Plain functions, same bytes as AceSLS/SLSsteam; both are called, not
			// hooked, by Moon with the same argument shapes as SLSsteam's calls.
			{ "CSteamEngine::PostCallback",
			  "E8 ? ? ? ? 4D 8B AE",
			  Follow::Relative, {} },
			{ "IClientUser::UpdateAppOwnershipTicket",
			  "41 89 CD 83 FD",
			  Follow::PrologueUpwards, { 0x55, 0x41, 0x56, 0x41, 0x57, 0x41 } },
			// steamui.so.  The ping-response interface is the base of CGameInfoDialog
			// at +8; its slot 0 is an adjustor thunk to the real ServerResponded.
			{ "ISteamMatchmakingPingResponse::ServerResponded", "", Follow::VtableSlot, {},
			  "15CGameInfoDialog", "29ISteamMatchmakingPingResponse", 0, {}, {}, true },
			// CRemoteClientManager's primary vtable: slot 3 references its own VPROF
			// scope name "CRemoteClientManager::RecvPkt".
			{ "CRemoteClientManager::RecvPkt", "", Follow::VtableSlot, {},
			  "20CRemoteClientManager", "", 3, {}, {}, false, "CRemoteClientManager::RecvPkt" },
			// Family Share filter (optional).  Slot 3 of CCMInterface is the CM message
			// handler SLSsteam hooks; the evidence string is weaker than the one above.
			{ "CCMInterface::RecvPkt", "", Follow::VtableSlot, {},
			  "12CCMInterface", "", 3, {}, {}, false, "23LegacyCDKeyRegistered_t" },
			// Located by its own log lines: both strings are loaded by this one function.
			{ "CDepotDownloadMgr::BYldRequestDepotManifest", "", Follow::StringXref, {},
			  "", "", -1, {}, {}, false, {},
			  { "CDepotDownloadMgr::BYldRequestDepotManifest(App: %d, Depot: %d, Manifest: %llu, branch: %s): Failed to get manifest request code, '%s'",
			    "CDepotDownloadMgr::BYldRequestDepotManifest: same manifest %llu requested twice" } },
			// ---- Located (audited offline); the ones below with bindAtRuntime=false have no 64-bit hook yet.
			// Each was matched to its i386 twin by shared strings AND by direct-caller
			// counts (tools/re/rosette.py, callers.py).
			// OnChunkUnpacked(context, chunk, source, location, detail, unpackResult), six register
			// arguments.  Two copies exist, as on i386 (twins 0x1521750 and its regparm sibling):
			// 0x13de1e0 (0 direct callers, 0x31a bytes) and 0x13de530 (1 caller, 0x900 bytes,
			// the same prologue and error paths with more inlined).  Strings alone cannot tell
			// them apart because the big one carries every string of the small one: the small one
			// is matched by its prologue (a fewest-called string tie-break resolved offline but
			// not in the live process), the big one by a string only it has.
			{ "CDepotDownloadMgr::OnChunkUnpacked[cdecl]",
			  "55 48 89 E5 41 57 41 56 49 89 D6 41 55 41 89 CD 41 54 4D 89 C4 53 48 89 FB 48 81 EC ? ? 00 00",
			  Follow::None, {} },
			{ "CDepotDownloadMgr::OnChunkUnpacked[regparm3]", "", Follow::StringXref, {},
			  "", "", -1, {}, {}, false, {},
			  { "OnChunkUnpacked", "posting write chunk, offset %llu" }, false, false, true },
			// "config changed : added/updated/removed depots": one function.  i386 twin 0x11d14e0.
			{ "CDepotDownloadMgr::EvaluateConfigChanges", "", Follow::StringXref, {},
			  "", "", -1, {}, {}, false, {},
			  { "AppID %u %sconfig changed : added depots %s",
			    "AppID %u %sconfig changed : updated depots %s",
			    "AppID %u %sconfig changed : removed depots %s" }, false, false, true },
			// Bound at runtime (hook ported: ManifestBind).  Arguments checked in the
			// disassembly: rdi ctx, rsi, edx appId, ecx depotId, r8 manifestId (the
			// "%s/depotcache/%d_%llu.manifest" asprintf takes ecx and r8), r9.
			// i386 twin 0x1187310: 4 direct callers, one of them PrepareDepotDownload (as here).
			{ "CDepotDownloadMgr::ProcessDepotManifest", "", Follow::StringXref, {},
			  "", "", -1, {}, {}, false, {},
			  { "WARNING! Deleted corrupt manifest file \"%s\" (%s)." } },
			// i386 twin 0x1187850: 2 direct callers on both sides.
			{ "CDepotDownloadMgr::PrepareDepotDownload", "", Follow::StringXref, {},
			  "", "", -1, {}, {}, false, {},
			  { "Failed to decrypt cached manifest %d_%llu",
			    "Failed to write cached manifest %d_%llu" } },
			// i386 twin 0x11783a0 (2 callers on both sides). Parses package records: the only
			// function that loads both the AppItemID assertion and the corrupt-package log.
			{ "CPackageInfoCache::LoadPackage", "", Follow::StringXref, {},
			  "", "", -1, {}, {}, false, {},
			  { "item.m_unAppItemID != -1", "Corrupt data in buffer for package %u, CLN %u," }, false },
			// CUtlMemory<uint32>::Grow, taken from its call in LoadPackage (grow AppIdVec by
			// `needed - capacity`). i386 twin 0x116e210; 129 callers here, 126 there.
			{ "CUtlMemory::Grow", "E8 ? ? ? ? 8B 43 50 8D 70 01 EB", Follow::Relative, {} },
			// steamui.so, live library removal.  RunFrame is slot 3 of CSteamUIAppController
			// (no direct caller, like its i386 twin; it shares 7 of 7 assertion strings).
			{ "CSteamUIAppController::RunFrame", "", Follow::VtableSlot, {},
			  "21CSteamUIAppController", "", 3, {}, {}, true, "CUtlHashMap::RemoveAt - Invalid index" },
			// (controller, appid, create).  Two functions carry both strings; the i386 twin
			// has 236 direct callers, this one 248 and the other overload 54.
			{ "CSteamUIAppController::GetAppByID", "", Follow::StringXref, {},
			  "", "", -1, {}, {}, true, {},
			  { "CSteamUIAppController::GetAppByID", "CSteamUIAppController::GetAppByID - Create" }, true },
			// (source, appid, change_flags).
			{ "CUpdateManager::MarkAppChange", "", Follow::StringXref, {},
			  "", "", -1, {}, {}, true, {},
			  { "CUpdateManager::MarkAppChange" } },
			// `mov edx,[r14+0x1c]` right before the eAppOwnershipFlags field is serialized in
			// FillInAppOverview (i386: +0x18, so +4 for the vtable pointer). The destination
			// register is the wildcard, the displacement and what follows are pinned.
			{ "CSteamApp::OwnershipFlagsReference",
			  "41 8B ? 1C 48 8B BD ? ? ? ? 48 8D 35 ? ? ? ? E8",
			  Follow::None, {}, "", "", -1, {}, {}, true },
			// License refresh helpers (CUser methods, `this` in rdi).  Found through the i386 twins:
			// Mark has 5 direct callers on both sides and the same murmur-style hash of the package
			// id; Process (2 callers on both sides) walks the pending vector (stride 0x50), marks
			// each entry, then calls Notify and returns true.  Notify is the only function that
			// carries both strings.  Member offsets (0x2380, 0x23c4) are wildcarded in the signatures.
			{ "CUser::MarkLicenseAsChanged",
			  "41 57 48 8D 87 ? ? 00 00 41 89 F7 41 56 41 55 41 54 55 53 48 83 EC ? 88 54 24 ? 8B 97 ? ? 00 00",
			  Follow::None, {} },
			{ "CUser::ProcessPendingLicenseUpdates",
			  "41 54 49 89 FC 55 53 8B 9F ? ? 00 00 83 EB 01 78 ? 48 63 C3 48 8D 2C 80 48 C1 E5 04",
			  Follow::None, {} },
			{ "CUser::NotifyLicensesUpdated", "", Follow::StringXref, {},
			  "", "", -1, {}, {}, false, {},
			  { "17LicensesUpdated_t", "SendAppLicenseUpdates" } },
			// CCompatManager is a member of CUser, built by CUser's constructor: `lea rax,[rbx+disp32]`
			// (the manager, 0x4b58 on this client) then `call ctor(rdi=manager, rsi=CUser)`; the ctor
			// 0x11e20d0 stores the vtable at 0x316b2a8 and has this one caller.  AppInfo-style in-place
			// decoding: the displacement is read from the instruction, never pinned here.
			{ "CUser::m_CompatManager",
			  "48 8D 83 ? ? 00 00 48 89 DE 48 89 C7 48 89 84 24 ? ? 00 00 E8 ? ? ? ? 66 0F 6F 1D ? ? ? ? 48 89 DE 48 C7 83 ? ? 00 00 FF FF FF FF",
			  Follow::None, {} },
			// Parental controls.  The settings receiver (i386 twin 0x1abe750) is the one function
			// with its three log lines; arguments (instance, settings, len, signature, len, a6, a7)
			// map straight onto the System V convention, the seventh on the stack.
			{ "ParentalSettingsReceived", "", Follow::StringXref, {},
			  "", "", -1, {}, {}, false, {},
			  { "Parental settings failed to parse for account ID %u",
			    "Failed to cache settings for account ID %u",
			    "Parental settings signature did not verify for account ID %u" } },
			// `test al,al; jne ok` after the signature check of the cached settings (i386 twin: the
			// function at 0x1abde10, same jne over the "did not verify for cached settings" log).
			{ "ParentalSignatureCheck",
			  "84 C0 75 ? 8B 93 ? ? 00 00 48 8D AC 24 ? ? 00 00 31 C0 48 8D 35 ? ? ? ? 48 89 EF E8",
			  Follow::None, {} },
			// CAPIJobRequestUserStats body, the job that applies the Player.GetUserStats#1 answer
			// (i386 twin 0x1bcdb70); the hook only logs its result.
			{ "CAPIJob::GetPlayerStats", "", Follow::StringXref, {},
			  "", "", -1, {}, {}, false, {},
			  { "Player.GetUserStats#1", "Unexpected StatID %hu for GameID %llu",
			    "[AppID %u] CAPIJobRequestUserStats - updated schema from server" } },
			// i386 twin 0x133cc30: 8 direct callers on both sides; its sibling that shares
			// the log line has 2.
			{ "CDepotDownloadMgr::BuildDepotDependency", "", Follow::StringXref, {},
			  "", "", -1, {}, {}, false, {},
			  { "Failed to initialize depot %u, manifest %llu (%s)" }, true },
			// GetOrAddAppData(cache, appid, create).  Taken from its call in CUser's license walk
			// (followed by the SHA compare below), which is unique.  The string xref used first
			// ("GetOrAddAppData" + three assertion strings, fewest-called tie-break against a twin
			// with 13 callers) resolved offline but not in the live process, where it matched
			// several functions.
			{ "CAppInfoCache::GetOrAddAppData",
			  "E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 8B ? 48 39 ? 20 75",
			  Follow::Relative, {} },
			// AppData layout, read in CUser's license walk (BCheckUserLicensesUpdate), which looks the
			// entry up with GetOrAddAppData(cache, appid, false): the SHA-1 is compared in three
			// pieces at +0x20/+0x28/+0x30 (i386: +0x1c), and the skip flag is the byte at +0x14
			// (i386: +0x10).  Both sites are decoded in place by AppInfoState; Follow::None keeps
			// the instruction address.
			{ "CAppInfoCache::ShaReference",
			  "48 39 ? 20 75 ? 48 8B ? 08 48 39 ? 28 75 ? 8B ? 10 39 ? 30",
			  Follow::None, {} },
			{ "CAppInfoCache::SkipFlagReference",
			  "0F B6 ? 14 48 83 ? 01 20 ? 24 ? 39 ? ?",
			  Follow::None, {} },
			// i386 twin 0x11716f0, one direct caller on both sides.
			{ "CAppInfoCache::ThreadedReadFromDisk", "", Follow::StringXref, {},
			  "", "", -1, {}, {}, false, {},
			  { "ThreadedReadFromDisk: wrong universe: %d",
			    "ThreadedReadFromDisk: invalid header version: 0x%x" }, false, false, true },
			// CJobMgr::BRouteMsgToJob carries two VPROF scopes, one per routing path.
			{ "CJobMgr::BRouteMsgToJob", "", Follow::StringXref, {},
			  "", "", -1, {}, {}, false, {},
			  { "CJobMgr::BRouteMsgToJob() - continue job", "CJobMgr::BRouteMsgToJob() - job" } },
			// Three overloads share the "InitFromPacket" VPROF scope (a plain one, one
			// that frees the previous body, and a devirtualising wrapper).  The one Moon
			// hooks is the public entry: 170 call sites on both i386 and x86_64, the
			// others are reached through the vtable or one wrapper.
			{ "CProtoBufMsgBase::InitFromPacket", "", Follow::StringXref, {},
			  "", "", -1, {}, {}, false, {},
			  { "CProtoBufMsg::InitFromPacket( IMsgNetPacket )" }, true },
			// Serialized size of a message (header + body, +8): `call [rax+0x48]` then
			// `lea eax,[rax+rbp+8]`.  Same shape and same two callers as the i386 function.
			{ "CProtoBufMsgBase::Send",
			  "FF 50 48 8D 44 28 08 48 83 C4 08 5B 5D C3",
			  Follow::PrologueUpwards, { 0xFB, 0x89, 0x48, 0x53, 0x55 } },
			{ "CWebSocketConnection::BBuildAndAsyncSendFrame",
			  "45 31 F6 83 7F",
			  Follow::PrologueUpwards, { 0x55, 0x41, 0x56, 0x41, 0x57, 0x41 } },
		};
		return table;
	}

	inline const Locator* find(std::string_view name)
	{
		for (const auto& locator : locators())
		{
			if (locator.name == name && locator.bindAtRuntime)
				return &locator;
		}
		return nullptr;
	}
}
