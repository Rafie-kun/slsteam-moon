#!/usr/bin/env bash
# Package the release zip that GitHub releases distribute.
#
# Produces:
#   dist/slsteam-moon-linux-<version>.zip            (ELF32 build, legacy 32-bit client)
#   dist/slsteam-moon-linux-<version>-x86_64.zip     (ELF64 build, steamrt64 client)
#   dist/slsteam-moon-linux-<version>-x86_64-host.zip  (with --host, never published)
#
# Layout inside the zip (matches what consumers and luatools-moon
# expect: a single top-level slsteam-moon-<version>/ directory containing
# setup.sh and bin/, and the assets setup.sh references):
#
#   slsteam-moon-<version>/
#     bin/
#       SLSsteam.so
#       library-inject.so
#       pattern-refresh
#     docs/
#       LICENSE/                   (third-party + slsteam-moon licenses)
#     res/
#       config.yaml                (default config copied to ~/.config/SLSsteam)
#     setup.sh                     (installer the user runs)
#     tools/
#       steamstub-bypass/          (helper called by setup.sh install_steamstub)
#         install-steamless.sh
#         run-steamless.sh
#         scan-all.sh
#       steamless-bin/             (Steamless kit, bundled verbatim — see
#                                   docs/LICENSE/steamless; enables offline
#                                   SteamStub DRM removal, no download)
#         Steamless.CLI.exe
#         Plugins/*.dll
#
# Requires: bin/SLSsteam.so + bin/library-inject.so + bin/pattern-refresh
# (run scripts/build.sh first).
#
# Usage:
#   scripts/package.sh                      # version from res/version.txt
#   scripts/package.sh --version 2.0        # explicit version
#   scripts/package.sh --host               # local test zip from a host build: skips the
#                                           # glibc 2.34 floor (release builds go through
#                                           # scripts/build.sh --portable and keep it)
#   scripts/package.sh --help

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

VERSION=""
HOST_BUILD=0
while [ $# -gt 0 ]; do
	case "$1" in
		--version) VERSION="${2:-}"; shift 2 ;;
		--host) HOST_BUILD=1; shift ;;
		-h|--help)
			sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'
			exit 0
			;;
		*) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
	esac
done

if [ -z "$VERSION" ]; then
	if [ -r res/version.txt ]; then
		VERSION="$(tr -d '[:space:]' < res/version.txt)"
	fi
fi
if [ -z "$VERSION" ]; then
	echo "could not determine version (no --version, no res/version.txt)" >&2
	exit 1
fi

# Sanity-check the binaries are present and look right.
for f in bin/SLSsteam.so bin/library-inject.so bin/pattern-refresh; do
	if [ ! -f "$f" ]; then
		echo "missing $f" >&2
		echo "build first:  scripts/build.sh   (or scripts/build.sh --host for dev)" >&2
		exit 1
	fi
done

# Empty/zero-byte binaries would silently produce a broken release zip.
if [ ! -s bin/SLSsteam.so ] || [ ! -s bin/library-inject.so ] \
   || [ ! -s bin/pattern-refresh ] || [ ! -x bin/pattern-refresh ]; then
	echo "binaries in bin/ are empty -- rebuild before packaging" >&2
	exit 1
fi

# bin/ is intentionally ignored by git, so an old non-empty binary can look
# valid after a source checkout. Refuse to package either artifact when any
# compile input is newer; release.sh builds first, while direct package.sh
# callers get an explicit instruction instead of a stale release.
check_binary_freshness() {
	local binary="$1"
	local label="$2"
	shift 2
	local stale
	stale="$(find "$@" -type f -newer "$binary" -print -quit 2>/dev/null)"
	if [ -n "$stale" ]; then
		echo "$label is older than build input: $stale" >&2
		echo "rebuild before packaging: scripts/build.sh" >&2
		exit 1
	fi
}
check_binary_freshness bin/SLSsteam.so SLSsteam.so \
	src include lib res Makefile scripts/build.sh scripts/_build-host.sh \
	scripts/_build-portable.sh scripts/Dockerfile scripts/check-sls-abi.sh embed-config.sh embed-version.sh
check_binary_freshness bin/library-inject.so library-inject.so \
	tools/library-inject Makefile
check_binary_freshness bin/pattern-refresh pattern-refresh \
	tools/pattern-refresh src/pattern_catalog.cpp src/pattern_catalog.hpp \
	src/utils/process_lock.hpp res/pattern-public-key.hex Makefile \
	scripts/build.sh scripts/_build-host.sh scripts/_build-portable.sh scripts/Dockerfile

if [ "$HOST_BUILD" = 1 ]; then
	SLSM_ALLOW_HOST_GLIBC=1 scripts/check-sls-abi.sh bin/SLSsteam.so
else
	scripts/check-sls-abi.sh bin/SLSsteam.so
fi
scripts/check-pattern-refresh-abi.sh bin/pattern-refresh

# The zip name carries the build class so an installer can pick the asset that
# matches the Steam client it will hook (EI_CLASS byte: 01 = ELF32, 02 = ELF64).
ZIP_SUFFIX=""
if [ "$(od -An -tx1 -j4 -N1 bin/SLSsteam.so | tr -d ' \n')" = 02 ]; then
	ZIP_SUFFIX="-x86_64"
fi
[ "$HOST_BUILD" = 1 ] && ZIP_SUFFIX="${ZIP_SUFFIX}-host"
ZIP_NAME="slsteam-moon-linux-${VERSION}${ZIP_SUFFIX}.zip"

PKG_DIR="dist/slsteam-moon-${VERSION}"
ZIP_PATH="dist/${ZIP_NAME}"

echo "==> staging $PKG_DIR"
rm -rf "$PKG_DIR" "$ZIP_PATH"
mkdir -p "$PKG_DIR/bin" "$PKG_DIR/docs" "$PKG_DIR/res" "$PKG_DIR/tools"

cp bin/SLSsteam.so       "$PKG_DIR/bin/"
cp bin/library-inject.so "$PKG_DIR/bin/"
cp bin/pattern-refresh   "$PKG_DIR/bin/"
cp setup.sh              "$PKG_DIR/"
cp res/config.yaml       "$PKG_DIR/res/"
cp -r docs/LICENSE       "$PKG_DIR/docs/"
cp -r tools/steamstub-bypass "$PKG_DIR/tools/"

# Desktop-coverage helpers: reconciliation engine, user-unit/drop-in manager and
# thin CLI. setup.sh installs all three under $SLSDIR.
cp tools/desktop-coverage.lib.sh       "$PKG_DIR/tools/desktop-coverage.lib.sh"
cp tools/launcher-shim.lib.sh          "$PKG_DIR/tools/launcher-shim.lib.sh"
cp tools/desktop-guardian-units.lib.sh "$PKG_DIR/tools/desktop-guardian-units.lib.sh"
cp ensure-desktop-coverage.sh          "$PKG_DIR/ensure-desktop-coverage.sh"

# Strip any build/IDE artefacts that may live alongside steamstub-bypass.
find "$PKG_DIR/tools/steamstub-bypass" -mindepth 1 \
	! -name '*.sh' \
	-type f -delete 2>/dev/null || true

# Bundle the Steamless kit verbatim so SteamStub DRM removal works
# offline — no install-time GitHub download (the silent failure mode
# behind "Application load error 6").  Steamless is CC BY-NC-ND 4.0:
# we ship it unmodified and attribute it (docs/LICENSE/steamless).
if [ ! -f tools/steamless-bin/Steamless.CLI.exe ]; then
	echo "missing tools/steamless-bin/Steamless.CLI.exe -- the Steamless kit" >&2
	echo "must be committed so releases are self-contained." >&2
	exit 1
fi
cp -r tools/steamless-bin "$PKG_DIR/tools/"

echo "==> zipping $ZIP_PATH"
if command -v zip >/dev/null 2>&1; then
	( cd dist && zip -qr -9 "$ZIP_NAME" "slsteam-moon-${VERSION}" )
else
	# No zip(1) here (it is not part of a default Debian install): same archive,
	# written with python3 so the executable bits survive.
	( cd dist && python3 - "$ZIP_NAME" "slsteam-moon-${VERSION}" <<'PY'
import os, sys, zipfile
out, top = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
    for root, dirs, files in os.walk(top):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(root, name)
            info = zipfile.ZipInfo.from_file(path)
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as fh:
                zf.writestr(info, fh.read(), zipfile.ZIP_DEFLATED, 9)
PY
	)
fi
# Publish a sha256 sidecar next to the asset. The installer on the paired
# luatools-moon branch fetches "<asset>.sha256" and refuses an asset that does
# not match it, so a truncated or altered archive is caught before it is unpacked
# and executed; an installer that predates that change ignores the sidecar. This
# detects transport and mirror damage, NOT a compromised publishing account —
# that needs a detached signature and a project signing key.
( cd dist && sha256sum "$ZIP_NAME" > "${ZIP_NAME}.sha256" )

# Show a quick listing so callers can eyeball the contents.
echo
unzip -l "$ZIP_PATH"
echo
echo "==> ready: $ZIP_PATH ($(du -h "$ZIP_PATH" | cut -f1))"
