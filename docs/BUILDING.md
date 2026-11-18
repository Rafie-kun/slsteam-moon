# Building slsteam-moon

slsteam-moon is a 64-bit shared library that hooks the modern Linux Steam
client (`steamrt64`) through `LD_AUDIT`. The build produces three artefacts:

- `bin/SLSsteam.so` — main hook library
- `bin/library-inject.so` — small audit helper that redirects libcurl
  loading to the system copy
- `bin/pattern-refresh` — native pre-launch metadata refresher

The 32-bit `ubuntu12_32` client is not supported. `-m32` builds are rejected
by `scripts/check-sls-abi.sh`, which every packaging run goes through.

## Quick start

| Goal                                   | Command                          |
| -------------------------------------- | -------------------------------- |
| Use it (don't build)                   | grab the latest [release][rel]   |
| Develop / iterate fast                 | `scripts/build.sh --host`        |
| Build a portable, release-grade binary | `scripts/build.sh --portable`    |
| Produce the release zip                | `scripts/release.sh`             |
| Install onto your own Steam            | `make install`                   |

[rel]: https://github.com/Rafie-kun/slsteam-moon/releases/latest

`make` on its own builds the same three binaries natively; it is the target
the container build runs.

## Prerequisites

The native build is `g++ -m64 -std=c++20`. You need an x86_64 host (or
container) and:

Ubuntu/Debian:

```
sudo apt install g++ make pkg-config libssl-dev libcurl4-openssl-dev zip
```

Fedora:

```
sudo dnf install gcc-c++ make pkgconf openssl-devel libcurl-devel zip
```

Arch:

```
sudo pacman -S base-devel pkgconf openssl curl zip
```

No 32-bit development packages and no `dpkg --add-architecture i386` step are
involved. The static archives the library links against
(`lib/liblibmem.a`, `lib/libprotobuf-lite.a`, `lib/libyaml-cpp.a`) are
committed as `elf64-x86-64` objects; re-check them with
`objdump -f lib/liblibmem.a` after replacing any of them.

## Build modes

### Host build (`scripts/build.sh --host` / `make`)

Compiles in place using the host toolchain. Fast, no container needed.

The catch: the binary's glibc requirement matches the system you build
on. A binary built on Ubuntu 24.04 (glibc 2.39) will not run on Ubuntu
22.04 (glibc 2.35). Use this for development on whatever distro you're
on; use the portable mode for anything you ship.

### Portable build (`scripts/build.sh --portable`)

Builds inside an Ubuntu 22.04 container (Podman or Docker), so the
resulting binary works on any x86_64 distro with **glibc ≥ 2.34**: Ubuntu
22.04+, Debian 12+, Fedora 36+, modern Mint, Pop!_OS, Zorin, etc.

The container is itself glibc 2.35, so "built on 22.04" is not by itself a
guarantee. `scripts/check-sls-abi.sh` enforces it: it reads the produced
library's symbol versions and fails the build if anything newer than
`GLIBC_2.34` is required. `scripts/package.sh` runs it on every artefact, so
a host build cannot be shipped as a portable release by accident.

This is what release builds use. Requires Podman (rootless, preferred)
or Docker installed; the script picks whichever it finds.

The container image (`slsteam-moon-builder`) is built from
`scripts/Dockerfile` on first run; subsequent runs reuse the cache.

### Cross-distro reach

The glibc minimum is determined by the base image plus the gate above. If you
need to support older distros, change the `FROM` line in
`scripts/Dockerfile` and lower the limit in `scripts/check-sls-abi.sh`
together — otherwise the gate will reject the build. The Makefile flags
already include `-D_GLIBCXX_USE_CXX11_ABI=0` for C++ ABI compatibility.

## Releasing

```
scripts/release.sh                    # version from res/version.txt
scripts/release.sh --version 3.2      # explicit version
```

Equivalent to `scripts/build.sh --portable` followed by
`scripts/package.sh`. The output is:

- `dist/slsteam-moon-linux-<ver>.zip` — the release asset
- `dist/slsteam-moon-linux-<ver>.zip.sha256` — sha256sum sidecar for it

matching the layout consumers (`luatools-moon/install.sh`) expect:

```
slsteam-moon-<ver>/
  bin/{SLSsteam.so, library-inject.so, pattern-refresh}
  setup.sh
  res/{config.yaml, release-version.txt}
  docs/LICENSE/
  tools/
```

`res/release-version.txt` is what the installed payload updater compares a
release tag against, so it must stay in step with the zip name.

Upload both files to the GitHub release with `gh`:

```
gh release upload vX.Y dist/slsteam-moon-linux-X.Y.zip \
                       dist/slsteam-moon-linux-X.Y.zip.sha256 --clobber
```

The tag must be `v<version>`; `.github/workflows/release.yml` builds the zip
from the tag on `push: tags: v*`.

## Verifying a build

```
scripts/check-sls-abi.sh bin/SLSsteam.so
scripts/check-pattern-refresh-abi.sh bin/pattern-refresh
scripts/check-compat.sh
```

The first two are the release gates (ELF64 x86-64, glibc ≤ 2.34).
`check-compat.sh` reports the binary's actual glibc minimum and whether it
will run on your current host.

By hand:

```
file bin/SLSsteam.so
readelf -h bin/SLSsteam.so | grep -E 'Class|Machine'
objdump -T bin/SLSsteam.so | grep GLIBC | sort -V | tail -5
ldd bin/SLSsteam.so
```

## Tests

```
make test-ascii test-audit-symbols test-memhlp-pic \
     test-memhlp-target test-memhlp-prologue
```

The full list is in the `Makefile`; each target compiles one standalone
harness under `tools/` and runs it. `test-optional-locators`,
`test-manifestpin-patterns` and `test-stats-audit` need a local Steam client
(`STEAMCLIENT` / `STEAMUI`, which default to the `steamrt64` paths) and are
not part of a plain checkout's CI run.

`.github/workflows/ci.yml` builds the library on `ubuntu-latest`, runs the
ABI gates and executes the harnesses that need neither Steam nor a client
binary.

## Troubleshooting

**`wrong ELF class: ELFCLASS32`** — you built a 32-bit object, or something
in the link picked up an i386 archive. The target is `steamrt64`, so every
input must be `elf64-x86-64`. Check the toolchain (`g++ -m64`) and the
checked-in `lib/*.a`.

**`GLIBC_X.YZ not found` at Steam launch** — the binary was built on a
newer system than the user's. Rebuild with `scripts/build.sh --portable`,
which drops the requirement back to the 2.34 floor.

**`cannot open shared object file` for libssl/libcurl** — these are host
libraries; install the 64-bit runtime packages:

```
sudo apt install libssl3 libcurl4         # Ubuntu/Debian
sudo pacman -S openssl curl               # Arch
```

**`No container runtime found`** — install podman (rootless, preferred) or
docker, or use `scripts/build.sh --host`.

**Docker permission denied** — add yourself to the docker group, or use
podman (rootless):

```
sudo usermod -aG docker $USER && newgrp docker
```

**`steamrt64/steamclient.so` not found by the locator tests** — those tests
need an installed, updated Steam client. Point `STEAMCLIENT` and `STEAMUI`
at the real paths if Steam lives outside `~/.local/share/Steam`.

## Known limitations

- `CServerPipe` (`src/sdk/CSteamEngine.hpp`) still carries its 32-bit field
  annotations. Only `pid` and `pipeHandle` are read, and `Process_t::init`
  rejects an implausible pid, but the 64-bit offsets have not been verified
  against `steamrt64/steamclient.so`. Do not add field reads without checking
  them against a live client.
- The SteamUI protobuf layout used by library removal is asserted only for
  i386; the x86_64 layout is unverified.
- The `Pattern_t` entries were verified against a `steamrt64` client at the time
  of the port. Re-run `test-optional-locators` and `test-manifestpin-patterns`
  after a client update before shipping a release.
- `res/updates.yaml` lists `steamclient.so` digests for the `ubuntu12_32`
  client. On x86_64 `SafeMode` cannot match them and fails closed, so it
  refuses to start Steam; enable it only once the file carries `steamrt64`
  digests.

## Build flags

From the Makefile:

```
-O3 -flto=auto -fPIC -m64 -std=c++20 -D_GLIBCXX_USE_CXX11_ABI=0
```

- `-m64` — the `steamrt64` client is 64-bit; `SLSsteam.so` must match it.
- `-D_GLIBCXX_USE_CXX11_ABI=0` — old C++ ABI for compatibility with
  the system `libstdc++.so.6` Steam loads.
- `-O3 -flto=auto` — size + speed.
- `-fPIC` — required for shared libraries.
- `-fno-reorder-blocks-and-partition` — keeps exception landing pads in the
  hot partition so `catch(...)` still routes to them.

Don't change these without a reason.
