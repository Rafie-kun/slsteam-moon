#include "achievements.hpp"
#include "stats_policy.hpp"
#include "playerstats.hpp"
#include "../config.hpp"
#include "../globals.hpp"
#include "../log.hpp"
#include "../sdk/CProtoBufMsgBase.hpp"
#include "../sdk/EResult.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace
{
struct Pending { uint32_t app, account; uint64_t epoch; bool modern; };
struct PendingUser { uint32_t app, account; uint64_t epoch; };
std::unordered_map<uint64_t, PendingUser> pendingUser;

struct GameInfo
{
	std::vector<std::pair<uint32_t, uint32_t>> achievements; // internal_key, groupid
	std::vector<uint32_t> groups;                            // groupid
};
std::unordered_map<uint64_t, uint32_t> pendingGame;          // job id -> app
std::unordered_map<uint32_t, GameInfo> gameInfo;             // app -> schema summary
std::mutex pendingMutex;
std::unordered_map<uint64_t, Pending> pending;
bool validJob(uint64_t job) { return job != 0 && job != UINT64_MAX; }
}

// At the serialized CM boundary the jobid is final, and CloudRedirect has
// already had the opportunity to serve the original SELF request. Work on
// our own protobuf objects, never Steam-owned strings/arenas.
std::vector<uint8_t> Achievements::rewriteRequest(bool modern,
	const uint8_t* data, uint32_t size, const CMsgProtoBufHeader& header)
{
	if (!g_config.achievements.get() || size > maxReplyBytes ||
	    header.ByteSizeLong() > maxHeaderBytes || !header.has_jobid_source() ||
	    !validJob(header.jobid_source())) return {};
	const uint32_t account = StatsPolicy::account();
	if (!account || (header.has_steamid() &&
	    !StatsPolicy::isSelf(header.steamid(), account)))
	{
		g_pLog->debugOnce("Achievements: request left alone, account=%u header_steamid=%llu\n",
		                  account, static_cast<unsigned long long>(header.steamid()));
		return {};
	}
	uint32_t app = 0;
	uint64_t target = 0;
	CMsgClientGetUserStats legacy;
	if (modern)
	{
		bool appSeen = false, targetSeen = false, valid = true;
		if (!PlayerStats::detail::walk(data, size, [&](const auto& field) {
			if (field.number != 1 && field.number != 2) return;
			bool& seen = field.number == 1 ? targetSeen : appSeen;
			if (seen || field.wireType != 0) { valid = false; return; }
			seen = true;
			std::size_t offset = field.valueOff;
			uint64_t value = 0;
			if (!PlayerStats::detail::readVarint(data, size, offset, value) ||
			    (field.number == 2 && (!value || value > UINT32_MAX))) valid = false;
		}) || !valid) return {};
		auto id = PlayerStats::parseRequestAppId(data, size);
		if (!id) return {};
		app = *id;
		target = PlayerStats::parseRequestSteamId(data, size).value_or(0);
	}
	else
	{
		if (!legacy.ParseFromArray(data, size) || !legacy.has_game_id() ||
		    legacy.game_id() > UINT32_MAX) return {};
		app = static_cast<uint32_t>(legacy.game_id());
		target = legacy.steam_id_for_user();
	}
	if (!StatsPolicy::isSelf(target, account)) return {};
	const uint64_t epoch = StatsPolicy::localEpoch(app, account, true);
	if (!epoch)
	{
		if (g_config.isAddedAppId(app))
			g_pLog->debugOnce("Achievements: app=%u is added but has no local epoch "
			                  "(not injected, not owned locally, or ownership not observed)\n", app);
		return {};
	}
	const uint64_t owner = resolveOwnerSteamId(app, g_config.achievementOwners.get(),
	                                          g_config.achievementOwnerId.get());
	if (!owner || StatsPolicy::isSelf(owner, account)) return {};
	std::vector<uint8_t> out;
	if (modern) out = PlayerStats::buildSpoofedRequest(owner, app);
	else
	{
		legacy.clear_crc_stats();
		legacy.set_schema_local_version(-1);
		legacy.set_steam_id_for_user(owner);
		out.resize(legacy.ByteSizeLong());
		if (!legacy.SerializeToArray(out.data(), out.size())) return {};
	}
	std::lock_guard lock(pendingMutex);
	// Retain unanswered jobs as bounded tombstones. Expiring by time would
	// allow a late owner reply through with its achievement progress intact.
	if (auto it = pending.find(header.jobid_source()); it != pending.end())
	{
		it->second.epoch = 0; // Ambiguous duplicate: any reply must fail safely.
		return {};
	}
	if (pending.size() >= 4096 || out.size() > maxReplyBytes) return {};
	pending.emplace(header.jobid_source(), Pending{app, account, epoch, modern});
	g_pLog->debugOnce("Achievements: schema request for app=%u redirected to owner %llu\n",
	                  app, static_cast<unsigned long long>(owner));
	return out;
}

std::optional<std::vector<uint8_t>> Achievements::rewriteResponse(bool modern,
	const uint8_t* data, uint32_t size, CMsgProtoBufHeader& header)
{
	if (!header.has_jobid_target() || !validJob(header.jobid_target())) return {};
	Pending request{};
	{
		std::lock_guard lock(pendingMutex);
		auto it = pending.find(header.jobid_target());
		if (it == pending.end() || it->second.modern != modern) return {};
		request = it->second;
		pending.erase(it);
	}
	g_pLog->debugOnce("Achievements: owner reply received for app=%u\n", request.app);
	const auto failure = [&]() -> std::vector<uint8_t> {
		g_pLog->debug("Achievements: reply for app=%u rejected (failing safe)\n", request.app);
		if (header.ByteSizeLong() > maxHeaderBytes)
		{
			const auto job = header.jobid_target();
			const auto steam = header.steamid();
			const auto session = header.client_sessionid();
			header.Clear();
			header.set_jobid_target(job);
			header.set_steamid(steam);
			header.set_client_sessionid(session);
		}
		header.set_eresult(2); // A rewritten request must NEVER leak another user's progress.
		if (modern) return {};
		CMsgClientGetUserStatsResponse body;
		body.set_game_id(request.app);
		body.set_eresult(2);
		std::vector<uint8_t> out(body.ByteSizeLong());
		body.SerializeToArray(out.data(), out.size());
		return out;
	};
	if (!g_config.achievements.get() || !request.epoch || size > maxReplyBytes ||
	    header.ByteSizeLong() > maxHeaderBytes ||
	    StatsPolicy::localEpoch(request.app, request.account, true) != request.epoch ||
	    (header.has_steamid() && !StatsPolicy::isSelf(header.steamid(), request.account)))
		return failure();
	if (modern)
	{
		if (!PlayerStats::detail::walk(data, size, [](const auto&) {}) ||
		    PlayerStats::fieldBytes(data, size, 3).empty()) return failure();
		std::vector<uint8_t> out;
		std::string layout, kept;
		PlayerStats::detail::walk(data, size, [&](const auto& field) {
			layout += std::to_string(field.number) + ":" + std::to_string(field.end - field.start) + " ";
			if (field.number != 2 && field.number != 4)
			{
				kept += std::to_string(field.number) + " ";
				out.insert(out.end(), data + field.start, data + field.end);
			}
		});
		g_pLog->debug("Achievements: owner reply app=%u eresult=%d fields[%s] kept[%s]\n",
		              request.app, header.has_eresult() ? header.eresult() : -1,
		              layout.c_str(), kept.c_str());
		g_pLog->debugOnce("Achievements: schema kept and progress stripped for app=%u\n", request.app);
		return out;
	}
	CMsgClientGetUserStatsResponse body;
	if (!body.ParseFromArray(data, size) || body.game_id() != request.app ||
	    !body.has_schema() || body.schema().empty()) return failure();
	body.clear_stats();
	body.clear_achievement_blocks();
	body.clear_crc_stats();
	body.set_eresult(ERESULT_OK);
	std::vector<uint8_t> out(body.ByteSizeLong());
	if (!body.SerializeToArray(out.data(), out.size())) return failure();
	return out;
}

void Achievements::recvMessage(uint32_t type, bool hasSteamId, uint64_t steamId)
{
	// ClientLogOnResponse=751, ClientLoggedOff=757, ClientLicenseList=780.
	if (type != 751 && type != 757 && type != 780) return;
	StatsPolicy::invalidate();
	if (type == 757) StatsPolicy::setAccount(0);
	else if (hasSteamId)
	{
		const auto account = static_cast<uint32_t>(steamId);
		if (steamId && StatsPolicy::isSelf(steamId, account)) StatsPolicy::setAccount(account);
	}
	// Keep bounded outstanding jobs so their late replies are rejected rather
	// than allowing the previously selected owner's progress through unchanged.
}

void Achievements::recvMessage(const CProtoBufMsgBase* msg)
{
	if (!msg) return;
	// Gate on type BEFORE touching msg->header: on the newer client an
	// untracked message can reach here with a header pointer that is not safe
	// to dereference, and only 751/757/780 ever need the header at all.
	if (msg->type != 751 && msg->type != 757 && msg->type != 780) return;
	const bool hasSteamId = msg->header && msg->header->has_steamid();
	recvMessage(msg->type, hasSteamId, hasSteamId ? msg->header->steamid() : 0);
}


namespace
{
// Unlocks recorded locally for one app: (block * 256 + bit, unix time), parsed from the
// binary KeyValues file Steam keeps in appcache/stats.  Layout (read from real files):
//   "cache" { "crc" "PendingChanges" "<block>" { "data" ... "AchievementTimes" { "<bit>" time } } }
struct LocalUnlock { uint32_t id; uint32_t time; };

std::string statsDir()
{
	const char* home = std::getenv("HOME");
	if (!home) return {};
	for (const char* root : {"/.steam/steam", "/.local/share/Steam", "/.steam/debian-installation"})
	{
		const std::string dir = std::string(home) + root + "/appcache/stats";
		std::error_code ec;
		if (std::filesystem::is_directory(dir, ec)) return dir;
	}
	return {};
}

struct KvReader
{
	const std::vector<uint8_t>& d;
	std::size_t i = 0;
	std::vector<LocalUnlock>& out;
	int depth = 0;

	bool cstr(std::string& s)
	{
		const auto end = std::find(d.begin() + static_cast<std::ptrdiff_t>(i), d.end(), uint8_t{0});
		if (end == d.end()) return false;
		s.assign(d.begin() + static_cast<std::ptrdiff_t>(i), end);
		i = static_cast<std::size_t>(end - d.begin()) + 1;
		return true;
	}

	// block < 0: not inside a stat block; inTimes: inside AchievementTimes of `block`.
	bool node(int block, bool inTimes)
	{
		if (++depth > 6) return false;
		while (i < d.size())
		{
			const uint8_t type = d[i++];
			if (type == 8) { --depth; return true; }
			std::string key;
			if (!cstr(key)) return false;
			if (type == 0)
			{
				int nextBlock = block;
				bool times = false;
				if (depth == 2 && key.size() <= 4 && !key.empty() &&
				    std::all_of(key.begin(), key.end(), [](char c) { return c >= '0' && c <= '9'; }))
					nextBlock = std::atoi(key.c_str());
				if (depth == 3 && key == "AchievementTimes") times = true;
				if (!node(nextBlock, times)) return false;
			}
			else if (type == 2 || type == 3 || type == 4 || type == 6)
			{
				if (i + 4 > d.size()) return false;
				int32_t value;
				std::memcpy(&value, d.data() + i, 4);
				i += 4;
				if (inTimes && block >= 0 && type == 2 && value > 0 && !key.empty() &&
				    std::all_of(key.begin(), key.end(), [](char c) { return c >= '0' && c <= '9'; }))
				{
					const int bit = std::atoi(key.c_str());
					if (bit >= 0 && bit < 256)
						out.push_back({static_cast<uint32_t>(block * 256 + bit),
						               static_cast<uint32_t>(value)});
				}
			}
			else if (type == 7 || type == 10)
			{
				if (i + 8 > d.size()) return false;
				i += 8;
			}
			else if (type == 1)
			{
				std::string ignored;
				if (!cstr(ignored)) return false;
			}
			else return false;
		}
		return false;
	}
};

std::vector<LocalUnlock> readLocalUnlocks(uint32_t account, uint32_t app)
{
	std::vector<LocalUnlock> out;
	const std::string dir = statsDir();
	if (dir.empty()) return out;
	std::ifstream in(dir + "/UserGameStats_" + std::to_string(account) + "_" + std::to_string(app) + ".bin",
	                 std::ios::binary);
	if (!in) return out;
	std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (data.empty() || data.size() > (1u << 20)) return out;
	KvReader reader{data, 0, out};
	reader.node(-1, false);
	std::sort(out.begin(), out.end(), [](const LocalUnlock& a, const LocalUnlock& b) { return a.id < b.id; });
	out.erase(std::unique(out.begin(), out.end(),
	          [](const LocalUnlock& a, const LocalUnlock& b) { return a.id == b.id; }), out.end());
	return out;
}

void putVarint(std::vector<uint8_t>& out, uint64_t v)
{
	while (v >= 0x80) { out.push_back(static_cast<uint8_t>(v | 0x80)); v >>= 7; }
	out.push_back(static_cast<uint8_t>(v));
}
} // namespace

void Achievements::noteUserAchievementsRequest(const uint8_t* data, uint32_t size,
	const CMsgProtoBufHeader& header)
{
	if (!g_config.achievements.get() || size > 64 || !header.has_jobid_source() ||
	    !validJob(header.jobid_source())) return;
	const uint32_t account = StatsPolicy::account();
	if (!account) return;
	uint64_t target = 0, app = 0;
	bool targetSeen = false, appSeen = false, valid = true;
	if (!PlayerStats::detail::walk(data, size, [&](const auto& field) {
		if (field.wireType != 0 || (field.number != 1 && field.number != 2)) return;
		std::size_t offset = field.valueOff;
		uint64_t value = 0;
		if (!PlayerStats::detail::readVarint(data, size, offset, value)) { valid = false; return; }
		if (field.number == 1) { target = value; targetSeen = true; }
		else { app = value; appSeen = true; }
	}) || !valid || !appSeen || !app || app > UINT32_MAX) return;
	if (targetSeen && !StatsPolicy::isSelf(target, account)) return;
	const uint32_t appId = static_cast<uint32_t>(app);
	const uint64_t epoch = StatsPolicy::localEpoch(appId, account, true);
	if (!epoch) return;
	std::lock_guard lock(pendingMutex);
	if (pendingUser.size() >= 4096) pendingUser.clear();
	pendingUser[header.jobid_source()] = PendingUser{appId, account, epoch};
}

void Achievements::noteGameAchievementsRequest(const uint8_t* data, uint32_t size,
	const CMsgProtoBufHeader& header)
{
	if (!g_config.achievements.get() || size > 64 || !header.has_jobid_source() ||
	    !validJob(header.jobid_source())) return;
	uint64_t app = 0;
	PlayerStats::detail::walk(data, size, [&](const auto& field) {
		if (field.number != 1 || field.wireType != 0) return;
		std::size_t offset = field.valueOff;
		PlayerStats::detail::readVarint(data, size, offset, app);
	});
	if (!app || app > UINT32_MAX || !g_config.isAddedAppId(static_cast<uint32_t>(app))) return;
	std::lock_guard lock(pendingMutex);
	if (pendingGame.size() >= 4096) pendingGame.clear();
	pendingGame[header.jobid_source()] = static_cast<uint32_t>(app);
}

void Achievements::noteGameAchievementsResponse(const uint8_t* data, uint32_t size,
	const CMsgProtoBufHeader& header)
{
	if (!header.has_jobid_target() || !validJob(header.jobid_target()) ||
	    size > 4 * maxReplyBytes) return;
	uint32_t app = 0;
	{
		std::lock_guard lock(pendingMutex);
		const auto it = pendingGame.find(header.jobid_target());
		if (it == pendingGame.end()) return;
		app = it->second;
		pendingGame.erase(it);
	}
	if (header.has_eresult() && header.eresult() != 1) return;
	GameInfo info;
	auto varintField = [](const uint8_t* d, std::size_t len, const auto& field) -> uint64_t {
		uint64_t value = 0;
		std::size_t offset = field.valueOff;
		if (field.wireType == 0) PlayerStats::detail::readVarint(d, len, offset, value);
		return value;
	};
	const bool ok = PlayerStats::detail::walk(data, size, [&](const auto& field) {
		if (field.wireType != 2 || (field.number != 1 && field.number != 3)) return;
		const uint8_t* inner = data + field.valueOff;
		const std::size_t innerLen = field.valueLen;
		if (field.number == 1)
		{
			uint32_t key = 0, group = 0;
			PlayerStats::detail::walk(inner, innerLen, [&](const auto& f) {
				if (f.number == 8) key = static_cast<uint32_t>(varintField(inner, innerLen, f));
				else if (f.number == 11) group = static_cast<uint32_t>(varintField(inner, innerLen, f));
			});
			info.achievements.emplace_back(key, group);
		}
		else
		{
			uint32_t group = 0;
			PlayerStats::detail::walk(inner, innerLen, [&](const auto& f) {
				if (f.number == 1) group = static_cast<uint32_t>(varintField(inner, innerLen, f));
			});
			info.groups.push_back(group);
		}
	});
	if (!ok || info.achievements.empty()) return;
	g_pLog->debug("Achievements: game list for app=%u: %zu achievement(s) in %zu group(s)\n",
	              app, info.achievements.size(), info.groups.size());
	std::lock_guard lock(pendingMutex);
	gameInfo[app] = std::move(info);
}

std::optional<std::vector<uint8_t>> Achievements::rewriteUserAchievementsResponse(
	const uint8_t* data, uint32_t size, const CMsgProtoBufHeader& header)
{
	if (!header.has_jobid_target() || !validJob(header.jobid_target())) return {};
	PendingUser request{};
	{
		std::lock_guard lock(pendingMutex);
		const auto it = pendingUser.find(header.jobid_target());
		if (it == pendingUser.end()) return {};
		request = it->second;
		pendingUser.erase(it);
	}
	if (size > maxReplyBytes || (header.has_eresult() && header.eresult() != 1)) return {};
	// Anything the server already reported (a game that is also owned natively) wins.
	bool hasEntries = false, valid = PlayerStats::detail::walk(data, size, [&](const auto& field) {
		if (field.number == 1) hasEntries = true;
	});
	if (!valid || hasEntries) return {};
	if (StatsPolicy::localEpoch(request.app, request.account, true) != request.epoch) return {};

	auto unlocks = readLocalUnlocks(request.account, request.app);
	if (unlocks.empty()) return {};

	// Match the unlocks against the game's own list so the answer only names achievements the
	// UI knows, and so each group can report how many of its achievements are unlocked.
	GameInfo game;
	bool haveGame = false;
	{
		std::lock_guard lock(pendingMutex);
		const auto it = gameInfo.find(request.app);
		if (it != gameInfo.end()) { game = it->second; haveGame = true; }
	}
	std::unordered_map<uint32_t, uint32_t> completedPerGroup;
	std::unordered_map<uint32_t, uint32_t> latestPerGroup;
	std::size_t dropped = 0;
	if (haveGame)
	{
		std::unordered_map<uint32_t, uint32_t> groupOf;
		for (const auto& [key, group] : game.achievements) groupOf[key] = group;
		std::vector<LocalUnlock> kept;
		for (const auto& unlock : unlocks)
		{
			const auto it = groupOf.find(unlock.id);
			if (it == groupOf.end()) { ++dropped; continue; }
			kept.push_back(unlock);
			++completedPerGroup[it->second];
			latestPerGroup[it->second] = std::max(latestPerGroup[it->second], unlock.time);
		}
		unlocks = std::move(kept);
		if (unlocks.empty())
		{
			g_pLog->debug("Achievements: none of the %zu local unlock(s) of app=%u match the game's list\n",
			              dropped, request.app);
			return {};
		}
	}
	std::vector<uint8_t> out(data, data + size);
	for (const auto& unlock : unlocks)
	{
		std::vector<uint8_t> entry;
		entry.push_back(0x08);
		putVarint(entry, unlock.id);
		entry.push_back(0x10);
		entry.push_back(0x01);
		entry.push_back(0x1d);
		for (int b = 0; b < 4; ++b) entry.push_back(static_cast<uint8_t>(unlock.time >> (8 * b)));
		out.push_back(0x0a);
		putVarint(out, entry.size());
		out.insert(out.end(), entry.begin(), entry.end());
	}
	if (haveGame)
	{
		std::unordered_map<uint32_t, uint32_t> totalPerGroup;
		for (const auto& [key, group] : game.achievements) { (void)key; ++totalPerGroup[group]; }
		for (const uint32_t group : game.groups)
		{
			const uint32_t completed = completedPerGroup[group];
			const bool done = completed != 0 && completed == totalPerGroup[group];
			std::vector<uint8_t> entry;
			entry.push_back(0x08); putVarint(entry, group);
			entry.push_back(0x10); entry.push_back(0x01);
			entry.push_back(0x18); putVarint(entry, completed);
			entry.push_back(0x20); entry.push_back(done ? 0x01 : 0x00);
			entry.push_back(0x4d);
			const uint32_t when = done ? latestPerGroup[group] : 0;
			for (int b = 0; b < 4; ++b) entry.push_back(static_cast<uint8_t>(when >> (8 * b)));
			out.push_back(0x22);
			putVarint(out, entry.size());
			out.insert(out.end(), entry.begin(), entry.end());
		}
	}
	g_pLog->debug("Achievements: filled GetUserAchievements for app=%u with %zu local unlock(s)%s, "
	              "%zu ignored as not in the game's list\n", request.app, unlocks.size(),
	              haveGame ? " and the group summary" : " (game list not seen yet, no group summary)", dropped);
	return out;
}
