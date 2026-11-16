#!/usr/bin/env bash
# ============================================================================
#  slsteam-moon-x64 uninstaller
# ============================================================================
#  One command, like the installer:
#    curl -fsSL https://github.com/ogb4n/slsteam-moon-x64/releases/latest/download/uninstall-x86_64.sh | bash
#
#  It runs the upstream luatools-moon uninstaller, which knows how to undo everything the
#  install did (library, wrapper, PATH lines, desktop entries and the system launcher shim,
#  guardian units, Lumen, the LuaTools plugin), and adds what that one does not do:
#
#    * copies ~/.config/SLSsteam (config, depot keys) to a backup folder BEFORE anything is
#      removed (SLSM_NO_BACKUP=1 to skip), because the upstream uninstaller deletes it;
#    * does not touch Millennium, which is not part of this stack (upstream would remove it);
#    * removes the root-owned launcher backup that the upstream uninstaller leaves behind
#      ("remove it manually"), but only after checking that the live launcher is back to the
#      original, so the only copy of it is never deleted;
#    * checks the result and exits non-zero, listing what is still there.
#
#  Kept on purpose: your game scripts (<Steam>/config/stplug-in/) and the backup it just made.
#  This script deletes no log itself; the upstream one removes ~/.SLSsteam.log (seen on a
#  real run) and may leave ~/.lumen.log.
#
#  Environment: LUATOOLS_UNINSTALL_URL / LUATOOLS_UNINSTALL_FILE (where the upstream script
#  comes from), LUATOOLS_UNINSTALL_SHA256 (refuse a script whose hash differs),
#  SLSM_NO_BACKUP=1.
# ============================================================================

set -uo pipefail

UPSTREAM_URL="${LUATOOLS_UNINSTALL_URL:-https://raw.githubusercontent.com/swwayps/luatools-moon/main/uninstall.sh}"
UPSTREAM_FILE="${LUATOOLS_UNINSTALL_FILE:-}"
UPSTREAM_SHA256="${LUATOOLS_UNINSTALL_SHA256:-}"
# Test hook: where the "live" launcher files are looked up (default: the real filesystem).
FS_ROOT="${SLSM_FS_ROOT:-}"
SLS_SHIM_TAG="# slsteam-moon system launcher shim"

say()  { printf '→ %s\n' "$*"; }
good() { printf '✓ %s\n' "$*"; }
warn() { printf '⚠ %s\n' "$*" >&2; }
die()  { printf '✗ uninstall-x86_64: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# A path we are about to delete must be exactly what we think it is.
# ---------------------------------------------------------------------------
home_is_sane() {
	[ -n "${HOME:-}" ] && [ "${HOME#/}" != "$HOME" ] && [ "$HOME" != / ] \
		&& [ "$(printf '%s' "$HOME" | tr -cd / | wc -c)" -ge 2 ] && [ -d "$HOME" ]
}

# ---------------------------------------------------------------------------
# 1. Backup of the configuration the upstream uninstaller deletes.
# ---------------------------------------------------------------------------
backup_config() {
	BACKUP_DIR=""
	[ "${SLSM_NO_BACKUP:-0}" = 1 ] && { say "SLSM_NO_BACKUP=1: no backup of ~/.config/SLSsteam"; return 0; }
	local src="$HOME/.config/SLSsteam"
	[ -d "$src" ] || { say "No ~/.config/SLSsteam to back up"; return 0; }
	BACKUP_DIR="$HOME/slsteam-moon-backup-$(date +%Y%m%d-%H%M%S)"
	mkdir -p -- "$BACKUP_DIR" && cp -a -- "$src" "$BACKUP_DIR/config-SLSsteam" \
		|| { BACKUP_DIR=""; die "could not back up $src: nothing was removed"; }
	good "Configuration copied to $BACKUP_DIR/config-SLSsteam"
}

# ---------------------------------------------------------------------------
# 2. The upstream uninstaller.
# ---------------------------------------------------------------------------
fetch_upstream() {
	local file
	if [ -n "$UPSTREAM_FILE" ]; then
		[ -f "$UPSTREAM_FILE" ] || die "LUATOOLS_UNINSTALL_FILE not found: $UPSTREAM_FILE"
		file="$UPSTREAM_FILE"
	else
		command -v curl >/dev/null 2>&1 || die "curl is required to fetch the upstream uninstaller"
		TMP="$(mktemp -d)" || die "mktemp failed"
		file="$TMP/uninstall.sh"
		curl --proto '=https' --proto-redir '=https' -fsSL --connect-timeout 15 --retry 3 \
			"$UPSTREAM_URL" -o "$file" || die "could not fetch $UPSTREAM_URL: nothing was removed"
	fi
	local sum; sum="$(sha256sum "$file" | awk '{print $1}')"
	if [ -n "$UPSTREAM_SHA256" ]; then
		[ "$sum" = "$UPSTREAM_SHA256" ] || die "upstream uninstaller sha256 does not match LUATOOLS_UNINSTALL_SHA256: nothing was removed"
	else
		say "upstream uninstaller sha256: $sum"
	fi
	UPSTREAM_SCRIPT="$file"
}

run_upstream() {
	# Its prompts (sudo) read the terminal, not the pipe this script may have been read from.
	local input=/dev/null
	if [ -t 0 ]; then input=/dev/stdin; elif (: </dev/tty) 2>/dev/null; then input=/dev/tty; fi
	LUATOOLS_MOON_KEEP_MILLENNIUM=1 bash "$UPSTREAM_SCRIPT" < "$input"
}

# ---------------------------------------------------------------------------
# 3. The launcher backup the upstream uninstaller cannot remove (it is root-owned).
# ---------------------------------------------------------------------------
is_shim() { [ -f "$1" ] && head -3 "$1" 2>/dev/null | grep -qF -- "$SLS_SHIM_TAG"; }

remove_launcher_leftover() {
	local dir="$HOME/.local/share/SLSsteam"
	[ -e "$dir" ] || return 0
	[ ! -L "$dir" ] && [ -d "$dir" ] || { warn "$dir is not a plain directory: left alone"; return 1; }

	# Only the launcher backup tree may be left; anything else is not ours to delete here.
	local other
	other="$(find "$dir" -mindepth 1 -not -path "$dir/system-launcher-backup" \
		-not -path "$dir/system-launcher-backup/*" -print -quit 2>/dev/null)"
	if [ -n "$other" ]; then
		warn "$dir still holds files other than the launcher backup (first: $other): left alone"
		return 1
	fi

	# Each backup is <tree>/<path>.orig, the original of /<path>. It may be deleted only if the
	# live file is back to exactly that original.
	local tree="$dir/system-launcher-backup" bak rel live found=0
	while IFS= read -r -d '' bak; do
		found=1
		rel="${bak#"$tree"}"; rel="${rel%.orig}"; live="$FS_ROOT$rel"
		if [ ! -f "$live" ]; then
			warn "$live is missing: the backup $bak is the only copy of the original launcher, kept"
			return 1
		fi
		if is_shim "$live"; then
			warn "$live is still the slsteam-moon shim: the backup $bak is kept. Restore with: sudo cp -- '$bak' '$rel'"
			return 1
		fi
		if ! cmp -s -- "$bak" "$live"; then
			warn "$live differs from its backup $bak: kept, compare them before deleting"
			return 1
		fi
	done < <(find "$tree" -type f -name '*.orig' -print0 2>/dev/null)
	[ "$found" = 1 ] || say "No launcher backup file inside $tree"

	# Plain rm first; sudo only for what root owns (the launcher backup is written by sudo).
	# -f matters: without it rm asks "remove write-protected file?" on a terminal, and with its
	# stderr hidden that question looked like a hang. The path is the one validated above.
	say "Removing the leftover launcher backup: $dir"
	rm -rf -- "$dir" </dev/null 2>/dev/null
	if [ -e "$dir" ]; then
		sudo rm -rf -- "$dir"
	fi
	if [ -e "$dir" ]; then
		warn "could not remove $dir; remove it with: sudo rm -rf -- '$dir'"
		return 1
	fi
	good "Removed $dir"
}

# ---------------------------------------------------------------------------
# 4. What must be gone, and what is kept.
# ---------------------------------------------------------------------------
verify_removed() {
	local bad=0 p rc
	for p in "$HOME/.local/share/SLSsteam" "$HOME/.local/share/Lumen" "$HOME/.config/SLSsteam" \
	         "$HOME/.local/state/slsteam-moon" \
	         "$HOME/.config/systemd/user/slsteam-desktop-guardian.service" \
	         "$HOME/.config/systemd/user/slsteam-desktop-guardian.path" \
	         "$HOME/.config/systemd/user/slsteam-desktop-guardian.timer"; do
		if [ -e "$p" ] || [ -L "$p" ]; then warn "still present: $p"; bad=1; fi
	done
	for rc in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
		if [ -f "$rc" ] && grep -qF 'SLSsteam/path' "$rc" 2>/dev/null; then
			warn "$rc still puts the slsteam-moon wrapper in PATH"; bad=1
		fi
	done
	if is_shim "$FS_ROOT/usr/bin/steam"; then warn "/usr/bin/steam is still the slsteam-moon shim"; bad=1; fi
	return "$bad"
}

main() {
	[ "$(id -u)" -ne 0 ] || die "do not run this as root; run it as your normal user"
	home_is_sane || die "HOME ('${HOME:-}') does not look like a user home directory"

	say "This removes slsteam-moon, Lumen and the LuaTools plugin and restores the Steam launchers."
	backup_config
	fetch_upstream
	run_upstream; local rc=$?
	[ "$rc" -eq 0 ] || warn "the upstream uninstaller exited with $rc; checking what is left"

	local bad=0
	remove_launcher_leftover || bad=1
	verify_removed || bad=1

	echo
	[ -z "${BACKUP_DIR:-}" ] || say "Your configuration is kept in $BACKUP_DIR"
	say "Kept on purpose: your game scripts (<Steam>/config/stplug-in/)"
	if [ -n "${TMP:-}" ]; then
		rm -f -- "$TMP/uninstall.sh"
		rmdir -- "$TMP" 2>/dev/null || true
	fi
	if [ "$bad" -eq 0 ] && [ "$rc" -eq 0 ]; then
		good "slsteam-moon is uninstalled. Start Steam normally."
		return 0
	fi
	warn "uninstall finished with problems: see the lines above"
	return 1
}

# Sourced by tests with SLSM_UNINSTALL_LIB_ONLY=1.
if [ -z "${SLSM_UNINSTALL_LIB_ONLY:-}" ]; then
	main "$@"
	exit $?
fi
