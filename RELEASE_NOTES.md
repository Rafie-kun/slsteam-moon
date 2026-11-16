# slsteam-moon-64 v3.0

64-bit (`x86_64` / `ELF64`) build of [swwayps/slsteam-moon](https://github.com/swwayps/slsteam-moon) for **`steamrt64`**-based Linux Steam clients.

The port itself was done by foodiemonster in `slssteam-moon-64`; that repo has since been taken down, so this fork carries it. Everything below is their work apart from the build/test fixes listed at the end.

## Install

Through LuaTools: use the fork's installer and it picks this release up by itself.

```bash
curl -fsSL https://raw.githubusercontent.com/Rafie-kun/luatools-moon/main/install.sh | bash
```

On top of an existing LuaTools / slsteam-moon setup instead:

```bash
curl -fsSL https://raw.githubusercontent.com/Rafie-kun/slsteam-moon/slsteam-moon/install-over-luatools.sh | bash
```

`~/.config/SLSsteam/` (config, manifests, library-added-times) and your LuaTools files are left alone either way.

## What's in v3.0

- `SLSsteam.so`, `library-inject.so` and `pattern-refresh` built as native ELF64 against `steamrt64/steamclient.so` and `steamrt64/steamui.so` (Steam build `1794536281`).
- `la_symbind64` auditing, 64-bit System V AMD64 detours/decoders and struct layouts (`CNetPacket`, `CUtlVector`, `ManifestBind`, `AppInfoState`, `DepotQuarantine`, `LibraryRemoval`, `CSteamEngine`, `IClientCompat`, `IClientUtils`).
- The 58 64-bit patterns, including separate `IClientUser::RunIPCFrame` and `IClientUserStats::RunIPCFrame` hooks.
- `setup.sh` understands `steamrt64` clients, skips the 32-bit `cloud_redirect.so` preload there, and uses a direct `libdbus-1.so.3` fallback for notifications inside `pressure-vessel`.
- `install-over-luatools.sh` for installing over an existing setup.

On top of the port this fork fixes three test/tooling targets so `make test-*` is green again on 64-bit: `originalBody()` accessor for the packet API, test helpers using the accessor API, and `LD_AUDIT` path quoting in the Makefile.

## Known limitations

- **64-bit clients only.** A 32-bit `ubuntu12_32` client has to stay on upstream `swwayps/slsteam-moon` v2.9.
- `CServerPipe` offsets are still flagged `TODO(steamrt64)` in the port, so pipe/pid/ticket reads are its shakiest part.
- `ReconcilePin` logs non-fatal warnings for two unlocated 64-bit offsets (`ctx-vector`, `target-local` fixes disabled).
- `cloud_redirect.so` is skipped on `steamrt64` while CloudRedirect stays 32-bit.
- `pattern-refresh`'s signed remote catalog targets 32-bit builds; this build relies on its embedded 64-bit patterns.
