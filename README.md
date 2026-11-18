# slssteam-moon-x64

64-bit (`x86_64`) build of [swwayps/slsteam-moon](https://github.com/swwayps/slsteam-moon) (itself a fork of [SLSsteam](https://github.com/AceSLS/SLSsteam)) for **`steamrt64`-based** Linux Steam clients, with the extra protocol handlers, SteamStub support and the Lua manifest importer.

The 64-bit port is [ogb4n/slssteam-moon-x64](https://github.com/ogb4n/slssteam-moon-x64). This fork carries that port plus the small fixes needed to get its test targets building again and to keep it working with current Steam clients.

## Installation (with LuaTools)

1. Make sure your Steam client is updated to the current **`steamrt64`** version (if you previously rolled back to `ubuntu12_32`, unlock/update Steam first).
2. Install **LuaTools** ([swwayps/luatools-moon](https://github.com/swwayps/luatools-moon)) as normal.
3. Run the installer script to patch `slssteam-moon-x64` on top of your existing setup (this preserves `~/.config/SLSsteam/` and all LuaTools files):
   ```bash
   curl -fsSL https://raw.githubusercontent.com/Rafie-kun/slsteam-moon/slsteam-moon/install-over-luatools.sh | bash
   ```
4. Launch Steam normally — you should see a **`SLSSteam-moon-x64`** notification confirming it loaded.

> **Note:** If LuaTools later overwrites `SLSsteam.so` with a 32-bit build during an update, simply re-run the `curl` command above.

LuaTools installs the x64 port from `ogb4n/slssteam-moon-x64` and falls back to this fork's releases when that repo has no reachable release. Force either one with `SLS_REPO=owner/repo` when running `install.sh`.

For building from source, see [`docs/BUILDING.md`](docs/BUILDING.md).

## Credits

Upstream:

- [AceSLS](https://github.com/AceSLS) and contributors —
  the hook framework, protobuf plumbing, ownership patches,
  family-share bypass, and the broader feature set this branch
  builds on.

Reference material:

- [SteamDatabase / SteamAppInfo](https://github.com/SteamDatabase/SteamAppInfo)
  — `appinfo.vdf` v41 format documentation.
- [atom0s / Steamless](https://github.com/atom0s/Steamless) —
  invoked by the wrapper-integration helper.
- [Midrags / SFF / LumaCore](https://github.com/Midrags/SFF) —
  Windows reference port for the `LoadPackage` patch logic.
- [OpenSteamTool](https://github.com/OpenSteam001/OpenSteamTool) —
  some features were ported from or inspired by this project.
  Full credit to its author.

## Support

Open an issue: https://github.com/Rafie-kun/slsteam-moon/issues
