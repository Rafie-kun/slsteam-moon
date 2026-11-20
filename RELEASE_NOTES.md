# slsteam-moon 3.3 (64-bit steamrt64)

Fixes the arch detection bug that caused `wrong ELF class: ELFCLASS64`
errors on systems where both steamrt64 and steamrt32 are installed (e.g.
CachyOS). The wrapper and installer previously checked which steamrt
directory existed on disk, but on mixed systems both can be present while
Steam actually runs the 32-bit client. Now they check the ELF class of
the actual `steam` binary being launched, so a 64-bit payload is never
injected into a 32-bit Steam process.

## What changed

- `slsm_client_class()` now reads the ELF class of the `$STEAM_BIN`
  binary instead of checking for `steamrt64` vs `ubuntu12_32` directory
  existence. This correctly identifies 32-bit clients even when
  steamrt64 is also installed.
- At install time, a 64-bit payload is refused on a 32-bit client with
  a clear message instead of silently breaking the hook.
- At launch time, if the payload and client arch disagree, injection is
  skipped with one clear line instead of `wrong ELF class` spam in
  Steam's log.

## Install

Through LuaTools: rerun the fork's installer and it picks this release up.

```bash
curl -fsSL https://raw.githubusercontent.com/Rafie-kun/luatools-moon/main/install.sh | bash
```

On top of an existing LuaTools / slsteam-moon setup instead:

```bash
curl -fsSL https://raw.githubusercontent.com/Rafie-kun/slsteam-moon/slsteam-moon/install-over-luatools.sh | bash
```
