# slsteam-moon 3.1 (64-bit steamrt64)

Same payload as 3.0. This release changes how it is installed and how it stays
in step with Steam, plus one small Lumen fix.

## The payload updates itself

The Steam wrapper now asks this repo for its latest release at launch — at most
once every six hours — checks the published sha256, and swaps in
`SLSsteam.so`, `library-inject.so` and `pattern-refresh` when a newer tag shows
up. The old files go to `~/.local/share/SLSsteam/backup/autoupdate-<time>/`.
Nothing else is touched, and anything unexpected (offline, no `unzip`, bad hash,
an install with no recorded version) leaves the files exactly as they were.
`SLSM_NO_AUTOUPDATE=1` turns the check off.

Why bother: a Steam client update moves the code SLSsteam finds by pattern
matching, so an old payload is what makes games stop launching right after the
client updates. Waiting for a manual reinstall is what made that a broken day;
now it is a payload swap between launches.

## Lumen

- Lumen's output goes to `~/.local/share/Lumen/lumen.log` instead of
  `/dev/null`, so a sidecar that never attached is at least readable.
- When the luatools-moon Lumen user service owns the sidecar, the wrapper no
  longer starts a second one.

## Install

Through LuaTools: rerun the fork's installer and it picks this release up.

```bash
curl -fsSL https://raw.githubusercontent.com/Rafie-kun/luatools-moon/main/install.sh | bash
```

On top of an existing LuaTools / slsteam-moon setup instead:

```bash
curl -fsSL https://raw.githubusercontent.com/Rafie-kun/slsteam-moon/slsteam-moon/install-over-luatools.sh | bash
```
