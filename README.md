# slssteam-moon-64

A 64-bit (`x86_64`) fork of [swwayps/slsteam-moon](https://github.com/swwayps/slsteam-moon) (originally forked from [SLSsteam](https://github.com/AceSLS/SLSsteam)), updated to work natively on newer **`steamrt64`-based** Linux Steam clients with extra protocol handlers, SteamStub support, and a Lua manifest importer.

## Installation (with LuaTools)

1. Make sure your Steam client is updated to the current **`steamrt64`** version (if you previously rolled back to `ubuntu12_32`, unlock/update Steam first).
2. Install **LuaTools** ([swwayps/luatools-moon](https://github.com/swwayps/luatools-moon)) as normal.
3. Run the installer script to patch `slssteam-moon-64` on top of your existing setup (this preserves `~/.config/SLSsteam/` and all LuaTools files):
   ```bash
   curl -fsSL https://raw.githubusercontent.com/foodiemonster/slssteam-moon-64/main/install-over-luatools.sh | bash
   ```
   *(If you use the `slsteam-moon` branch directly, replace `/main/` with `/slsteam-moon/`.)*
4. Launch Steam normally — you should see a **`SLSSteam-moon-64`** notification confirming it loaded.

> **Note:** If LuaTools later overwrites `SLSsteam.so` with a 32-bit build during an update, simply re-run the `curl` command above.

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

Open an issue: https://github.com/foodiemonster/slssteam-moon-64/issues
