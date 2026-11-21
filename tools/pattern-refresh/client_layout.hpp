#pragma once

// Which directory of a Steam root holds the client modules Steam will load.
// Mirrors steam.sh and setup.sh's slsm_client_dir(): the SteamRT 64-bit client
// (steamrt64/) is the default; the sentinel .steam-disable-steamrt64-client, or
// STEAM_FORCE_CLIENT=ubuntu12_32|scout|ldlp, selects the legacy 32-bit client
// (ubuntu12_32/); STEAM_FORCE_CLIENT=steamrt64|steamrt3c|steamrt wins over the
// sentinel.  Without steamrt64/steam and a client there, only the legacy layout exists.

#include <filesystem>
#include <string_view>
#include <system_error>

namespace ClientLayout
{
	inline std::filesystem::path clientDirectory(
		const std::filesystem::path& steamRoot, std::string_view forceClient)
	{
		namespace fs = std::filesystem;
		if (forceClient == "ubuntu12_32" || forceClient == "scout" || forceClient == "ldlp")
			return steamRoot / "ubuntu12_32";

		std::error_code error;
		const bool force64 = forceClient == "steamrt64" || forceClient == "steamrt3c"
		                  || forceClient == "steamrt";
		if (!force64 && fs::exists(steamRoot / ".steam-disable-steamrt64-client", error))
			return steamRoot / "ubuntu12_32";

		error.clear();
		const auto launcher = steamRoot / "steamrt64" / "steam";
		const auto status = fs::status(launcher, error);
		const bool executable = !error && fs::is_regular_file(status)
		    && (status.permissions() & (fs::perms::owner_exec | fs::perms::group_exec
		                                | fs::perms::others_exec)) != fs::perms::none;
		if (executable && fs::is_regular_file(steamRoot / "steamrt64" / "steamclient.so", error))
			return steamRoot / "steamrt64";
		return steamRoot / "ubuntu12_32";
	}
}
