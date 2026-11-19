# slsteam-moon 3.2 (64-bit steamrt64)

The payload is rebuilt against the current Steam client, with the x86_64
fixes from the port merged in. Most of this release is about refusing the
wrong architecture instead of failing quietly.

## Architecture

- The wrapper and `setup.sh` read the ELF class of `SLSsteam.so` and the
  running `steamclient.so`. If they disagree, injection is skipped with one
  clear line instead of a pile of `wrong ELF class` lines in Steam's log.
- The installer refuses to drop a payload built for the other client arch
  over a working install, and the updater applies the same rule to release
  assets.
- The 32-bit `cloud_redirect.so` is no longer preloaded into a 64-bit client.

## Recovery mode

- The "paused itself for this session" latch only arms for launches that
  actually had a display. A scripted or remote launch can no longer put a
  graphical session into recovery mode.
- The latch clears when the payload changes, so reinstalling is enough to
  get out of it.

## Updates

- Releases are read from this fork first. jsDelivr is the download fallback,
  and its archive URLs must be commit-pinned.

## Install

Through LuaTools: rerun the fork's installer and it picks this release up.

```bash
curl -fsSL https://raw.githubusercontent.com/Rafie-kun/luatools-moon/main/install.sh | bash
```

On top of an existing LuaTools / slsteam-moon setup instead:

```bash
curl -fsSL https://raw.githubusercontent.com/Rafie-kun/slsteam-moon/slsteam-moon/install-over-luatools.sh | bash
```
