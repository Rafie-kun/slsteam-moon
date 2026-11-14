# slssteam-moon-64 v3.0

Native 64-bit (`x86_64` / `ELF64`) port of [swwayps/slsteam-moon](https://github.com/swwayps/slsteam-moon) for **`steamrt64`**-based Linux Steam clients.

## Quick Install (on top of LuaTools)

1. Make sure your Steam client is updated to `steamrt64` (not rolled back to `ubuntu12_32`).
2. Install **LuaTools** as normal.
3. Run:
   ```bash
   curl -fsSL https://raw.githubusercontent.com/foodiemonster/slssteam-moon-64/main/install-over-luatools.sh | bash
   ```
   *(Replace `main` with `slsteam-moon` if installing directly from that branch.)*

Your existing `~/.config/SLSsteam/` settings, manifests, and LuaTools files are preserved automatically.

## What's Changed in v3.0

### Native `steamrt64` (`x86_64`) Support
- Built `SLSsteam.so`, `library-inject.so`, and `pattern-refresh` as native `ELF64` x86-64 binaries targeting `steamrt64/steamclient.so` and `steamrt64/steamui.so` (tested on Steam build `1794536281`).
- Added `la_symbind64` auditing support and 64-bit System V AMD64 detour signatures, instruction decoders, and struct layouts (`CNetPacket`, `CUtlVector`, `ManifestBind`, `AppInfoState`, `DepotQuarantine`, `LibraryRemoval`, `CSteamEngine`, `IClientCompat`, `IClientUtils`).
- Located and verified all 58 64-bit patterns (`steamclient.so` + `steamui.so`), including distinct `IClientUser::RunIPCFrame` and `IClientUserStats::RunIPCFrame` hooks.

### SteamRT `pressure-vessel` & Installer Improvements
- Updated `setup.sh` to detect `steamrt64` clients and skip preloading 32-bit `cloud_redirect.so` on 64-bit Steam.
- Added direct `libdbus-1.so.3` (`org.freedesktop.Notifications.Notify`) fallback so `SLSSteam-moon-64` desktop notifications work inside SteamRT's `pressure-vessel` container.
- Fixed false-positive `unzip` missing warnings inside `pressure-vessel` by checking `/run/host/usr/bin` and `/run/host/bin`.
- Added `install-over-luatools.sh` for one-command installation over existing LuaTools / `slsteam-moon` setups.

## Known Limitations
- **32-bit `ubuntu12_32` Steam**: This release targets `steamrt64` only. Rolled-back 32-bit Steam clients should continue using upstream `swwayps/slsteam-moon`.
- **`ReconcilePin`**: Logs non-fatal warnings for two unlocated 64-bit offsets (`ctx-vector` and `target-local` fixes disabled).
- **`cloud_redirect.so`**: Skipped on `steamrt64` while CloudRedirect remains 32-bit.
- **`pattern-refresh`**: Upstream's signed remote catalog targets 32-bit builds; `slssteam-moon-64` relies on its embedded 64-bit patterns unless a 64-bit catalog is hosted.

