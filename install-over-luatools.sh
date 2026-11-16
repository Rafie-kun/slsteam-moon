#!/usr/bin/env bash
# Install slssteam-moon-64 on top of an existing LuaTools / slsteam-moon setup.
#
# Preserves all existing ~/.config/SLSsteam/ data (config.yaml, manifests,
# library-added-times.txt) and LuaTools / ACCELA files while upgrading
# ~/.local/share/SLSsteam/ to the 64-bit steamrt64 binaries and wrapper.
#
# Usage:
#   ./install-over-luatools.sh                  # download latest GitHub release
#   ./install-over-luatools.sh --zip <file.zip> # install from a local release zip
#   ./install-over-luatools.sh --from-repo <dir># install from a built repo dir
#   ./install-over-luatools.sh --help

set -euo pipefail

REPO_SLUG="Rafie-kun/slsteam-moon"
ZIP_PATH=""
REPO_DIR=""

usage() {
	cat <<'EOF'
Usage: install-over-luatools.sh [OPTIONS]

Installs slssteam-moon-64 on top of an existing LuaTools / slsteam-moon setup,
preserving ~/.config/SLSsteam/ and LuaTools configuration.

Options:
  --zip <path>        Install from a local slsteam-moon-linux-*.zip archive
  --from-repo <dir>   Install from a built slssteam-moon-64 repository directory
  -h, --help          Show this help message
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		--zip)
			ZIP_PATH="${2:-}"
			[ -n "$ZIP_PATH" ] || { echo "Error: --zip requires a path" >&2; exit 2; }
			shift 2
			;;
		--from-repo)
			REPO_DIR="${2:-}"
			[ -n "$REPO_DIR" ] || { echo "Error: --from-repo requires a directory" >&2; exit 2; }
			shift 2
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "Error: unknown option '$1' (try --help)" >&2
			exit 2
			;;
	esac
done

# 1. Verify steamrt64 client is present
STEAM_ROOT=""
for candidate in "$HOME/.steam/steam" "$HOME/.steam/debian-installation" "$HOME/.local/share/Steam"; do
	if [ -f "$candidate/steamrt64/steamclient.so" ] && [ -f "$candidate/steamrt64/steamui.so" ]; then
		STEAM_ROOT="$candidate"
		break
	fi
done

if [ -z "$STEAM_ROOT" ]; then
	echo "Error: 64-bit Steam client (steamrt64) was not found." >&2
	echo "Make sure Steam is updated to steamrt64 (not rolled back to ubuntu12_32) before installing slssteam-moon-64." >&2
	exit 1
fi
echo "Found steamrt64 client at: $STEAM_ROOT"

# 2. Back up existing 32-bit SLSsteam.so if present (never touch ~/.config/SLSsteam)
SLS_DIR="$HOME/.local/share/SLSsteam"
if [ -f "$SLS_DIR/SLSsteam.so" ]; then
	elf_class="$(od -An -tx1 -j4 -N1 "$SLS_DIR/SLSsteam.so" 2>/dev/null | tr -d '[:space:]')"
	if [ "$elf_class" = "01" ]; then
		mkdir -p "$SLS_DIR/backup"
		cp -f "$SLS_DIR/SLSsteam.so" "$SLS_DIR/backup/SLSsteam.so.32bit.bak"
		echo "Backed up existing 32-bit SLSsteam.so to $SLS_DIR/backup/SLSsteam.so.32bit.bak"
	fi
fi

# 3. Locate or download the 64-bit package
TMP_DIR=""
cleanup() {
	if [ -n "$TMP_DIR" ] && [ -d "$TMP_DIR" ]; then
		rm -rf "$TMP_DIR"
	fi
}
trap cleanup EXIT

INSTALL_ROOT=""

if [ -n "$REPO_DIR" ]; then
	INSTALL_ROOT="$REPO_DIR"
elif [ -n "$ZIP_PATH" ]; then
	[ -f "$ZIP_PATH" ] || { echo "Error: zip not found: $ZIP_PATH" >&2; exit 1; }
elif [ -x "./setup.sh" ] && [ -f "./bin/SLSsteam.so" ] && [ -f "./bin/library-inject.so" ]; then
	elf_class="$(od -An -tx1 -j4 -N1 "./bin/SLSsteam.so" 2>/dev/null | tr -d '[:space:]')"
	if [ "$elf_class" = "02" ]; then
		INSTALL_ROOT="$(pwd)"
	fi
fi

if [ -z "$INSTALL_ROOT" ]; then
	for tool in curl unzip; do
		command -v "$tool" >/dev/null 2>&1 || {
			echo "Error: '$tool' is required to download/extract the release." >&2
			exit 1
		}
	done

	TMP_DIR="$(mktemp -d)"

	if [ -z "$ZIP_PATH" ]; then
		echo "Fetching latest release info from github.com/$REPO_SLUG..."
		API_URL="https://api.github.com/repos/$REPO_SLUG/releases/latest"
		DOWNLOAD_URL="$(curl -fsSL "$API_URL" | grep -Eo 'https://[^"]+\.zip' | head -n 1 || true)"
		if [ -z "$DOWNLOAD_URL" ]; then
			echo "Error: could not find a .zip release asset at $API_URL." >&2
			echo "If no GitHub release is published yet, build locally and run:" >&2
			echo "  ./install-over-luatools.sh --from-repo /path/to/slssteam-moon-64" >&2
			exit 1
		fi
		ZIP_PATH="$TMP_DIR/slssteam-moon-64.zip"
		echo "Downloading $DOWNLOAD_URL..."
		curl -fL "$DOWNLOAD_URL" -o "$ZIP_PATH"
	fi

	unzip -q "$ZIP_PATH" -d "$TMP_DIR/extracted"
	INSTALL_ROOT="$(find "$TMP_DIR/extracted" -maxdepth 2 -name setup.sh -printf '%h\n' | head -n 1)"
	if [ -z "$INSTALL_ROOT" ] || [ ! -x "$INSTALL_ROOT/setup.sh" ]; then
		echo "Error: setup.sh not found inside $ZIP_PATH" >&2
		exit 1
	fi
fi

# 4. Verify payload is ELF64 before installing
if [ ! -f "$INSTALL_ROOT/bin/SLSsteam.so" ]; then
	echo "Error: $INSTALL_ROOT/bin/SLSsteam.so is missing (build first?)" >&2
	exit 1
fi
elf_class="$(od -An -tx1 -j4 -N1 "$INSTALL_ROOT/bin/SLSsteam.so" 2>/dev/null | tr -d '[:space:]')"
if [ "$elf_class" != "02" ]; then
	echo "Error: $INSTALL_ROOT/bin/SLSsteam.so is not an ELF64 binary." >&2
	exit 1
fi

# 5. Run setup.sh install (preserves ~/.config/SLSsteam/ and LuaTools files)
echo "Installing slssteam-moon-64 from $INSTALL_ROOT..."
(cd "$INSTALL_ROOT" && ./setup.sh install < /dev/null)

# 6. Post-install verification
installed_class="$(od -An -tx1 -j4 -N1 "$SLS_DIR/SLSsteam.so" 2>/dev/null | tr -d '[:space:]')"
if [ "$installed_class" != "02" ]; then
	echo "Error: installed $SLS_DIR/SLSsteam.so is not ELF64!" >&2
	exit 1
fi

echo ""
echo "Done! slssteam-moon-64 (ELF64) is installed over your existing setup."
echo "Preserved config: $HOME/.config/SLSsteam/"
echo "Note: If LuaTools updates/overwrites SLSsteam back to 32-bit in the future, re-run this script."

