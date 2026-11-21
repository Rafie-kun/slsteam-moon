#pragma once
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

class CProtoBufMsgBase;
class CMsgProtoBufHeader;

namespace Achievements
{
inline constexpr uint32_t maxReplyBytes = 262144;
inline constexpr uint32_t maxHeaderBytes = 1024;
inline uint64_t resolveOwnerSteamId(uint32_t app,
	const std::unordered_map<uint32_t, uint64_t>& perApp, uint64_t defaultOwner)
{
	const auto it = perApp.find(app);
	return it != perApp.end() && it->second ? it->second : defaultOwner;
}

// Empty request output / nullopt response means byte-for-byte passthrough.
// A present empty response is a correlated failure with header.eresult=Fail.
// Correlation uses final CM job IDs, never app ID alone.
std::vector<uint8_t> rewriteRequest(bool modern, const uint8_t* data,
	uint32_t size, const CMsgProtoBufHeader& header);
std::optional<std::vector<uint8_t>> rewriteResponse(bool modern, const uint8_t* data,
	uint32_t size, CMsgProtoBufHeader& header);
void recvMessage(const CProtoBufMsgBase* msg);

// Player.GetUserAchievements#1: the library page builds its Achievements panel and the
// "x/N" counter from this answer. For a game this account does not own server-side it is
// empty even though the unlocks are kept in appcache/stats/UserGameStats_<account>_<app>.bin,
// so the request is remembered and the empty answer is filled from that file.
//   Request  { uint64 steamid=1; uint32 appid=2; }
//   Response { repeated Achievement achievements=1 { uint32 id=1; bool achieved=2;
//              fixed32 unlock_time=3; } ... }   id = statBlock * 256 + bit
void noteUserAchievementsRequest(const uint8_t* data, uint32_t size,
	const CMsgProtoBufHeader& header);
// Player.GetGameAchievements#1 {1: appid, 2: language} -> {1: achievements{8: internal_key,
// 11: groupid}, 3: groups{1: groupid, 8: total}}: remembered for the games we fill, so the
// synthesized user answer can carry the group summary (groups{1 groupid, 2 achievable,
// 3 completed, 4 is_completed, 9 time}) the panel computes its "x/N" from.
void noteGameAchievementsRequest(const uint8_t* data, uint32_t size,
	const CMsgProtoBufHeader& header);
void noteGameAchievementsResponse(const uint8_t* data, uint32_t size,
	const CMsgProtoBufHeader& header);
std::optional<std::vector<uint8_t>> rewriteUserAchievementsResponse(
	const uint8_t* data, uint32_t size, const CMsgProtoBufHeader& header);

// Transport-agnostic form used by the CNetPacket CM-receive path, which has
// the login/logoff/license eMsg and the parsed header's steamid but no
// CProtoBufMsgBase. Behaviour matches the CProtoBufMsgBase overload.
void recvMessage(uint32_t type, bool hasSteamId, uint64_t steamId);
}
