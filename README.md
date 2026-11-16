# slsteam-moon-x64

64-bit-only continuation of [swwayps/slsteam-moon](https://github.com/swwayps/slsteam-moon),
itself a fork of [SLSsteam](https://github.com/AceSLS/SLSsteam). It targets the 64-bit
Steam client (steamrt64), now the default on Linux. The 32-bit client is not supported
here; for it, use the upstream repository.

Ported to x86_64 and proven on a real client: manifest binding, live add/remove
of a game, package patching, the LuaTools/Lumen buttons, the achievements panel for
added games, and downloading a non-owned title. Hooks installed but not yet exercised:
reconcile pin, depot quarantine, parental restrictions and the stats redirect. Details
and the full status table: [`docs/X86_64.md`](docs/X86_64.md).

## Install

### Requirements

- **System:** desktop Linux on x86_64 with glibc 2.34 or newer (the library is built on
  Ubuntu 22.04). Debian 13 is the tested system; see
  [Supported platforms](#supported-platforms) (SteamOS is not supported).
- **Steam:** the **64-bit client**, already started once so that
  `~/.local/share/Steam/steamrt64/` exists. If that folder is missing, Steam either has not
  started yet or is still on the 32-bit client, which this repository does not support (use
  the upstream repository).
- **Tools:** `bash`, `curl` and `sudo`, plus `unzip` or `python3` to unpack the downloads
  (`sha256sum` and `gzip` ship with the base system). Run it as your normal user, not as root.
- **Libraries:** the system `libcurl` and OpenSSL (Debian: `libcurl4 libssl3`; Arch:
  `curl openssl`). On a regular distribution the upstream installer installs missing
  packages itself, with `sudo`.
- **Network:** access to GitHub, since the installer downloads this repository's release, the
  upstream installer, Lumen and the LuaTools plugin.
- **Account:** the installer stops Steam and asks for your `sudo` password once.

**1. If you already have the upstream (32-bit) slsteam-moon / luatools-moon, remove it first.**
Run the upstream uninstaller, which cleans the old library, wrapper, launcher patches,
CloudRedirect hook, Lumen and the LuaTools plugin:

```sh
curl -fsSL https://raw.githubusercontent.com/swwayps/luatools-moon/main/uninstall.sh | bash
```

It also deletes `~/.config/SLSsteam` (your `config.yaml` and depot keys): copy that folder
first if you want to keep it. In the clean reinstall this was tested with, the Lua scripts of
the games you added (`<Steam>/config/stplug-in/`) were left in place. Installing over an
existing 32-bit install without this step has not been tested. Skip the step on a machine
that never had slsteam-moon.

**2. Install, in one command:**

```sh
curl -fsSL https://github.com/ogb4n/slsteam-moon-x64/releases/latest/download/install-x86_64.sh | bash
```

The script is a release asset, so it only changes after the CI has passed. It downloads
the latest x86_64 release of this repository, then runs the upstream
luatools-moon installer with the 64-bit pieces swapped in (Lumen and the LuaTools plugin
come from their upstream repositories). CloudRedirect is skipped because its hook is a
32-bit library: Steam Cloud is disabled for added games (`DisableCloud: yes`).

**3. Start Steam** from your applications menu. The first launch can take a while.

### If the installer stops with "forces the 32-bit client"

The installer refuses to continue (nothing is changed) when it finds that your Steam is set
to the legacy 32-bit client, or that no 64-bit client is installed, and it tells you which:

- a file `.steam-disable-steamrt64-client` in your Steam folder: delete it;
- an environment variable `STEAM_FORCE_CLIENT=ubuntu12_32` (or `scout`, `ldlp`) in your shell
  configuration, `.desktop` files or `/etc/environment`: remove it and open a new terminal;
- no `steamrt64/` folder: start Steam once and let it update, then run the installer again.

Both settings are often left over from trying to downgrade Steam. If you really want the
upstream 32-bit build, use the upstream repository, or run the installer with
`SLSM_ALLOW_STOCK=1` to let it install that build.

## Supported platforms

- **Tested:** a Debian 13 desktop (x86_64, X11 session) with the 64-bit Steam client, from
  uninstall to install to a running Steam. The build was also compiled on Arch Linux in a
  container; it was not run under Steam there.
- **Not supported, never tested: SteamOS, Steam Deck and its Gaming Mode, Decky Loader,
  Bazzite and other immutable distributions.** The upstream installer that this one wraps can
  change how Steam is launched in Gaming Mode (its "Game Mode hook"); that part was not
  adapted or tried here. A user reported that the install broke Decky's frontend in Gaming
  Mode ([issue #1](https://github.com/ogb4n/slsteam-moon-x64/issues/1)). Do not install on
  such a system unless you accept that risk.
- If it already happened, the upstream `uninstall.sh` from step 1 is meant to remove the
  Game Mode launcher hook; this was not verified on SteamOS.

## Update and uninstall

- **Update:** use Lumen's About tab, "Update All", or rerun the install command above.
  The installer repoints the few places where Lumen hard-codes the upstream repositories
  (the update check, the Update All button, the auto-fix offer) to this repository, so an
  update installs the latest x86_64 release here and never the 32-bit upstream one. If a
  Lumen update changes that text, the installer warns and exits with an error.
- **Uninstall, in one command:**

  ```sh
  curl -fsSL https://github.com/ogb4n/slsteam-moon-x64/releases/latest/download/uninstall-x86_64.sh | bash
  ```

  It runs the upstream `uninstall.sh` (which undoes the library, wrapper, `PATH` lines, desktop
  entries and the system launcher shim, guardian units, Lumen and the LuaTools plugin) and adds
  what that script does not do: it copies `~/.config/SLSsteam` (config, depot keys) to
  `~/slsteam-moon-backup-<date>/` first (`SLSM_NO_BACKUP=1` to skip), leaves Millennium alone,
  removes the root-owned launcher backup that upstream leaves behind (only once `/usr/bin/steam`
  is back to the original, so the only copy of it is never deleted), then checks the result and
  exits non-zero, listing what is still there. It stops Steam and asks for your `sudo` password.
  It keeps your game scripts (`<Steam>/config/stplug-in/`). The upstream script removes
  `~/.SLSsteam.log` and may leave `~/.lumen.log`. Tested against a stand-in upstream script and a throwaway home; the upstream
  script itself was run for real on Debian 13 only.
- **Offline or local zip:** `./install-x86_64.sh --slsteam-zip slsteam-moon-linux-<version>-x86_64.zip`.

Build: [`docs/BUILDING.md`](docs/BUILDING.md). Upstream history is kept; the 32-bit code
is still in the tree and will be removed in a separate change.

## Releases

Every push to `main` that changes code is built, tested and released by
[GitHub Actions](.github/workflows/release.yml). The release is published only if all of
these pass: the portable build on Ubuntu 22.04, the check that the binaries are 64-bit and
need glibc 2.34 at most, the `test-ci` unit tests (`make -k test-ci`), the installer and
auto-fix tests, and a check of the archive. The version is the UTC build time
(`v<YYYYMMDDHHMMSS>`) and the tag points at the tested commit. Pull requests run the same
build and tests without releasing. Pushes that only change documentation do not release.

The pipeline cannot run Steam: the offline locator audit (`make test-patterns-x86_64`) and
the load test need a real `steamrt64` client on disk, so run them yourself on a machine with
Steam before relying on a change that touches hooks or locators.

## Credits

Upstream:

- [swwayps](https://github.com/swwayps) and contributors, slsteam-moon: the
  protocol handlers, SteamStub support and Lua manifest importer this port is built on.
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

Open an issue: https://github.com/ogb4n/slsteam-moon-x64/issues
