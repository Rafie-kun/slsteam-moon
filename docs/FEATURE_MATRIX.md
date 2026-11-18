# Feature matrix after the x86_64 adoption

Compared at Moon `c3ed5a4` on `fix/x64-abi-stability`, `AceSLS/SLSsteam` `main`
and `Rafie-kun/SLSsteam` `fix/added-app-achievements`.

`Rafie-kun/SLSsteam` is upstream `AceSLS/SLSsteam` plus one feature: the branch
differs from `main` only in `src/feats/achievements.cpp` (+13/-4). Everything
marked AceSLS therefore applies to it unchanged.

| Subsystem | Moon x86_64 | AceSLS / Rafie-kun SLSsteam | Classification |
|---|---|---|---|
| LD_AUDIT injection | `la_symbind32` and `la_symbind64`, `la_version` echoed from the loader | `la_symbind32`/`64`, `la_version` echoes | required, kept |
| Pipe/process registry | `CServerPipe` x86_64 layout, `plausibleServerPipe()` gate | x86_64 layout (source of the adopted offsets) | required, adopted |
| `CNetPacket` overlay | `0x8/0x10/0x18`, size `0x30` | not present | architecture-specific, unresolved |
| `CUtlVector`/`CUtlMemory` | offsets by pointer width | by pointer width | required |
| Member-offset reads (`getUser`, `getUtils`) | signed disp32, sign-extended | `int32_t` read, `this + offset` (i386 semantics) | architecture-specific, adopted |
| Compat mapping (`CompatLive`) | present, register-agnostic REX.W decoder | not present | compatible, kept |
| Library removal | SteamUI `FillInAppOverview` ownership offset, per-arch decoder | not present | compatible, kept |
| Achievements | present | present (fork branch differs slightly) | required, kept |
| FakeAppId, tickets | present | present | required, kept |
| DLC, depot keys, manifests | depot key import/index/scope, manifest donor/pins/synthesis, quarantine | `dlc.cpp` only | Moon extension, kept |
| Hot reload, appinfo, parental, prewarm, provision, reconcile | present | not present | Moon extension, kept |
| Lumen CEF port rewrite | `cefport.hpp` plus the `exec*`/`posix_spawn` hooks | not present | project-specific, preserved |
| Lua plugin host (`lua.cpp`, LuaJIT + LuaBridge) | absent | present | intentionally excluded: LuaTools is the external plugin host |
| AArch64 pattern decompiler (`decompiler.cpp`) | absent | present | architecture-specific: Moon builds x86_64 only |
| Portable build, ABI gates, release workflow, mirror | present | not present | project-specific, preserved |

## What the migration lost

Nothing. Moon is a superset of both SLSsteam trees for every feature it ships.
The two subsystems Moon does not carry are intentional exclusions, not
regressions: the embedded Lua host is replaced by LuaTools, and the AArch64
decompiler only exists to resolve patterns on a target Moon does not build.

## What stays unverified

- `CNetPacket`: this fork measured `0x8/0x10/0x18` (size `0x30`) against the
  client it ports for; the independent `slsteam-moon-64` port measured
  `0x10/0x18/0x20` (size `0x28`) on steamrt64 `steamclient.so` build
  `a57ea463`. The offsets are validated at runtime, so a wrong base fails
  closed, but the conflict is not settled.
- `CServerPipe`: the adopted 64-bit offsets are AceSLS's reverse engineering,
  not a measurement against the `steamrt64` build this fork ships for. They are
  asserted per arch and gated by `plausibleServerPipe()`.
- No build or test has been run against a Steam client from this workspace; see
  "Known limitations" in `docs/BUILDING.md`.
