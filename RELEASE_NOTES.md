# slsteam-moon 3.4 (64-bit steamrt64)

Fixes the Steam crash on systems where both steamrt64 and steamrt32 are
installed (e.g. CachyOS). The wrapper previously checked the ELF class of
`steamrt64/steam` (the 64-bit launcher binary), but Steam re-execs from
that into `steamrt3/steam` (32-bit client). The 64-bit `SLSsteam.so` was
then injected via `LD_AUDIT` into a 32-bit process, and `ld.so` rejected
it with `wrong ELF class: ELFCLASS64`, crashing Steam on launch.

## What changed

- `slsm_client_class()` now checks both the launched `steam` binary AND
  whether `steamrt3/steam` exists and is 32-bit. If the 32-bit client is
  present, the wrapper reports the client as 32-bit and refuses to inject
  the 64-bit payload — Steam runs vanilla instead of crashing.
- At install time, a 64-bit payload is refused on a system where the
  32-bit client may run, with a clear message instead of a silent break.
- At launch time, if the payload and client arch disagree, injection is
  skipped with one clear line instead of `ld.so` error spam.

## Install

Through LuaTools: rerun the fork's installer and it picks this release up.

```bash
curl -fsSL https://raw.githubusercontent.com/Rafie-kun/luatools-moon/main/install.sh | bash
/usr/bin/steam
```

On top of an existing LuaTools / slsteam-moon setup instead:

```bash
curl -fsSL https://raw.githubusercontent.com/Rafie-kun/slsteam-moon/slsteam-moon/install-over-luatools.sh | bash
```
