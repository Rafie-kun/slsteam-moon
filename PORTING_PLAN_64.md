# Porting Plan: `slssteam-moon` → `slssteam-moon-64` (`steamrt64`)

## Goal
Port `slssteam-moon` from 32-bit `i386` (`ubuntu12_32`) to 64-bit `x86_64` (`steamrt64`) so `SLSsteam.so` and `library-inject.so` natively load into and hook the modern 64-bit Linux Steam client without requiring a client rollback.

---

## Codebase Audit Summary
Based on inspecting `slssteam-moon-64`:
- **Reusable (~70–80%)**:
  - High-level feature modules in `src/feats/` (AppInfo/VDF, CM/Protobufs, depot keys, manifest management, DLC/ownership/family-share/ticket/achievements, hot-reload, config/logging/curl).
  - Protobuf definitions and generated files in `src/sdk/protobufs/`.
  - Core SDK value types (`CSteamID`, `EResult`, `EReleaseState`, `CProtoBufMsgBase`, `CWebSocketFrame`, `CNetPacket`).
  - Existing test harness in `tools/test_*.cpp` and `tools/pattern-refresh/`.
- **Must Be Rebuilt / Ported (~20–30%)**:
  - **Static dependencies (`lib/`)**: `liblibmem.a`, `libprotobuf-lite.a`, and `libyaml-cpp.a` are currently `elf32-i386` archives.
  - **Build & packaging (`Makefile`, `scripts/`)**: Currently hardcodes `-m32` and `i386` pkg-config paths.
  - **Memory/relocation helpers (`src/memhlp.cpp`, `src/hooks.cpp`)**: Uses 32-bit PIC thunk relocation (`MemHlp::fixPICThunkCall`) and i386 calling/register assumptions.
  - **Patterns & binary layouts (`src/patterns.cpp`, `src/vftableinfo.hpp`, `src/sdk/`)**: Contains **62 `Pattern_t` signatures** written for 32-bit `steamclient.so` / `steamui.so`, plus 32-bit vtable indices and struct offsets.
  - **Launcher & injection (`tools/library-inject/`, `setup.sh`)**: Targets 32-bit `ubuntu12_32/steam` instead of `steamrt64`.

---

## Phased Porting Plan

### Phase 1 — 64-bit Toolchain, Dependencies & Clean Build
**Objective:** Compile `SLSsteam.so` and `library-inject.so` as valid `ELF64 x86-64` shared objects.

1. **Replace `lib/*.a` with `x86_64` static libraries**:
   - Build or vendor 64-bit static archives for:
     - `liblibmem.a` (`x86_64` Linux)
     - `libprotobuf-lite.a` (matching `tools/protoc-3.15` / `include/google/protobuf` headers)
     - `libyaml-cpp.a`
   - Verify with `objdump -f lib/*.a` that all objects report `elf64-x86-64`.
2. **Update `Makefile` and `scripts/`**:
   - Replace `-m32` with `-m64` (or parameterize `ARCH_FLAGS ?= -m64`) across `Makefile`, `scripts/_build-host.sh`, `scripts/_build-portable.sh`, and `scripts/Dockerfile`.
   - Point `STEAMCLIENT` / `STEAMUI` defaults to `steamrt64` paths (e.g. `~/.local/share/Steam/steamrt64/steamclient.so` or `ubuntu12_64` where applicable — confirm exact `steamrt64` layout from a live `steamrt64` Steam install).
   - Remove `i386` pkg-config overrides and use native 64-bit `libssl` / `libcurl`.
3. **Fix 64-bit compiler warnings/errors**:
   - Audit pointer-to-integer casts (`uint32_t` ↔ `uintptr_t` / `lm_address_t`), format specifiers (`%u` / `%x` → `%zu` / `PRIxPTR`), and struct packing/alignment across `src/`.

**Exit Criterion:** `make` produces `ELF64` `bin/SLSsteam.so` and `bin/library-inject.so` with zero linker errors.

---

### Phase 2 — x86-64 Hooking & Trampoline Relocation Layer
**Objective:** Ensure `libmem` detours and displaced instruction fixups work cleanly on `x86_64` PIC code.

1. **Replace `MemHlp::fixPICThunkCall` (`src/memhlp.cpp`, `src/hooks.cpp`)**:
   - On `i386`, GCC emits `call __x86.get_pc_thunk.bx; add ebx, imm32` to compute GOT-relative addresses.
   - On `x86_64`, GCC/Clang emit **RIP-relative** instructions (`lea reg, [rip + disp32]`, `mov reg, [rip + disp32]`, `jmp/call [rip + disp32]`).
   - Verify whether the updated `x86_64` `liblibmem.a` already relocates RIP-relative prologue instructions in its trampoline, or implement a `fixRIPRelativeInsn()` pass in `MemHlp` for any displaced `0x48 0x8D / 0x8B / 0xFF` RIP-relative operands.
2. **Audit Hook Signatures & ABI (`src/hooks.cpp`)**:
   - Confirm all hook callbacks use standard System V AMD64 ABI (no `__attribute__((cdecl))` / `__attribute__((thiscall))` / stack-cleanup mismatches).
   - Verify `uintptr_t` / `size_t` vs `uint32_t` parameter types on pointer-sized arguments (e.g. buffer sizes, handles, `CUser*`, `void*`).
3. **Verify `LD_AUDIT` / `LD_PRELOAD` Entry (`src/audit_symbols.hpp`, `tools/library-inject/`)**:
   - Confirm `la_version` / `la_symbind64` (instead of `la_symbind32`) is exported/implemented if `LD_AUDIT` is used on `x86_64`.

**Exit Criterion:** A minimal smoke-test build of `SLSsteam.so` injects into the 64-bit `steamrt64` process and logs initialization without crashing.

---

### Phase 3 — Re-Reversing Patterns, VTables & Struct Offsets (`steamrt64`)
**Objective:** Update all **62 `Pattern_t` entries** (`src/patterns.cpp`), `VFTIndexes` (`src/vftableinfo.hpp`), and internal class offsets (`src/sdk/`) against a live 64-bit `steamrt64` `steamclient.so` and `steamui.so`.

Break this into incremental, testable milestones:

1. **Core Engine & Boot / IPC (`Milestone 1`)**:
   - `CSteamEngine` / `ProcessIPCFrame` / `SetAppIdForCurrentPipe`
   - `CProtoBufMsgBase::Send` / `CNetPacket` receive hooks
   - `CUser` base & `ParentalSignatureCheck` / `ParentalSettingsReceived`
   - *Verify:* Steam boots cleanly with `SLSsteam.so` loaded, logs CM/IPC traffic, and does not increment `boot_fail_count`.
2. **AppInfo, Ownership & Library Visibility (`Milestone 2`)**:
   - `CUser::CheckAppOwnership`, `CUser::GetSubscribedApps`, `CUser::PostCallbackToAppId`
   - `IClientApps::GetAppData`, `appinfostate` / `appinfo_vdf` / `appinfo_provision` locators
   - `VFTIndexes::IClientAppManager`, `VFTIndexes::IClientApps`, `VFTIndexes::IClientUtils`
   - *Verify:* Added apps appear in the Steam library and AppInfo publishes without blockers.
3. **DLC, Compat & Tickets (`Milestone 3`)**:
   - `IClientAppManager::IsAppDlcInstalled`, `BIsDlcEnabled`, `BCanRemotePlayTogether`, `LaunchApp`
   - `CUser::Offset_CompatManager` & `VFTIndexes::IClientCompat` (`SpecifyCompatTool`, `GetCompatToolName`)
   - `CAPIJob::GetPlayerStats`, `ticket`, `steamstub`
   - *Verify:* DLC unlocks, Proton/compat tool selection works, and `tools/test_clientcompat.cpp` passes against `steamrt64`.
4. **Depot Keys, Manifests & Downloads (`Milestone 4`)**:
   - `CDepotDownloadMgr::BuildDepotDependency`
   - `reconcilepin`, `manifestbind`, `manifestid`, `depotkey` locators
   - *Verify:* Downloads resolve manifests and decrypt depots; `tools/test_reconcilepin_pattern.cpp` & `tools/test_manifestpin_patterns.cpp` pass.

**Tip:** Leverage `tools/pattern-refresh/` and add/adapt unit locator tests (`tools/test_*.cpp`) so every updated pattern is validated offline against the `steamrt64` `steamclient.so` binary before launching Steam.

---

### Phase 4 — Runtime Injection, Container Compatibility & Packaging
**Objective:** Make installation and launching work seamlessly on `steamrt64` across distros.

1. **Launcher & Runtime Path Updates (`setup.sh`, `tools/launcher-shim.lib.sh`)**:
   - Detect and launch the 64-bit `steamrt64` Steam binary instead of forcing `ubuntu12_32`.
   - Verify `pressure-vessel` / Steam Linux Runtime container env propagation (`LD_AUDIT` / `LD_PRELOAD` and bind mounts if running inside `steamrt64` runtime).
2. **Packaging & Release (`scripts/package.sh`, `scripts/release.sh`)**:
   - Package `slssteam-moon-64-linux-<ver>.zip` with clear version/arch metadata so tools like **SteamRollbackr** and **luatools-moon** can detect `x86_64` vs `i386` builds cleanly.
   - Update `README.md` and `docs/BUILDING.md` with 64-bit build prerequisites.

---

## Current Progress
- **Phase 1 (64-bit Toolchain, Dependencies & Clean Build): Complete**
  - Built 64-bit `ELF64 x86-64` targets (`bin/SLSsteam.so`, `bin/library-inject.so`, `bin/pattern-refresh`) via `make -j$(nproc) build`.
- **Phase 2 (x86-64 Hooking & Trampoline Relocation Layer): Complete**
  - Added `la_symbind64` (`src/main.cpp`), `__x86_64__` guard in `MemHlp::fixPICThunkCall` (`src/memhlp.cpp`), System V AMD64 hook signatures (`reconcilepin.cpp`, `depotquarantine.cpp`, `appinfostate.cpp`), and 64-bit instruction decoders (`CSteamEngine.cpp`, `IClientUtils.cpp`, `compatlive.hpp`, `libraryremoval_policy.hpp`, `appdata_layout.hpp`).
  - All 11 unit test suites (`test-appinfostate`, `test-libraryremoval`, `test-clientcompat`, `test-pattern-refresh`, `test-reconcilepin-scan`, `test-audit-symbols`, `test-patternscan`, `test-memhlp-pic`, `test-memhlp-target`, `test-memhlp-prologue`, `test-pattern-catalog`) pass.
- **Phase 3 (Re-Reversing Patterns & Struct Offsets for `steamrt64`): Complete (Static/Offline Verified)**
  - Located and verified all 58 64-bit `Pattern_t` entries (`SteamUI`: 6, `steamclient.so`: 52) uniquely against `steamrt64/steamclient.so` and `steamrt64/steamui.so` under `#if defined(__x86_64__)` in `src/patterns.cpp`.
  - `test-optional-locators` passes against `/home/javier/.local/share/Steam/steamrt64/{steamclient.so,steamui.so}` (`CSteamApp::OwnershipFlags offset = 0x1c`).
- **Phase 4 (Runtime Injection, Launcher & Packaging): Complete**
  - Fixed live `steamrt64` (`1794536281`) runtime issues: `IClientUser::RequiresLegacyCDKey` pattern, `CSteamEngine::GetServerPipe` pattern, `CNetPacket` 64-bit layout (`OFF_BODY = 0x8`, `OFF_SIZE = 0x10`, `OFF_ORIGBODY = 0x18`, `sizeof == 0x30`), and `ManifestBind` SysV AMD64 arity (`DepotFn_t`, 8-arg `BuildDepFn_t`, 64-bit `CUtlVector` offsets `0x08` / `0x10`).
  - Updated `setup.sh` (`guard_client_fp`, `slsm_pattern_steam_root`, ELF64 check on `cloud_redirect.so`), `tools/pattern-refresh/catalog.cpp` (`steamrt64` module lookup with `ubuntu12_32` fallback), `scripts/check-sls-abi.sh` (`ELF64` / `Advanced Micro Devices X86-64`), and `scripts/check-compat.sh` (`64-bit`).
  - Live 20-second `STEAM_FORCE_CLIENT=steamrt64` smoke test with `LD_AUDIT="bin/library-inject.so:bin/SLSsteam.so"` ran cleanly (`RC=124`, zero crashes, `[Notify] Loaded successfully`, app ownership tickets stamped and apps unlocked in `~/.SLSsteam.log`).


