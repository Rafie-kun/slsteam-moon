#!/bin/sh
# slsm-collect-logs.sh — gather slsteam-moon / Lumen / Steam logs into one zip.
#
# Usage:
#   slsm-collect-logs.sh [output.zip]
#
# Prints the output zip path to stdout. Sensitive data (home dir, session
# tokens, LD_AUDIT values) is scrubbed before packaging so users can safely
# share the archive with a developer.

set -eu

OUT="${1:-$HOME/.local/state/slsteam-moon/logs-$(date +%s).zip}"
TMP="$(mktemp -d)"
trap 'rm -rf "${TMP:-}"' EXIT

LOG_DIR="$TMP/logs"
mkdir -p "$LOG_DIR"

# ── Log files we care about ─────────────────────────────────────────────────
steam_log="$HOME/.local/share/Steam/logs/console-linux.txt"
lumen_log="$HOME/.local/share/Lumen/lumen.log"
guard_log="$HOME/.local/state/slsteam-moon/guard.log"
cover_log="$HOME/.local/share/SLSsteam/guardian.log"

copy_if_exists() {
	src="$1"; dst="$2"
	[ -f "$src" ] && cp -f "$src" "$dst" 2>/dev/null || true
}

copy_if_exists "$steam_log"    "$LOG_DIR/console-linux.txt"
copy_if_exists "$lumen_log"    "$LOG_DIR/lumen.log"
copy_if_exists "$guard_log"    "$LOG_DIR/guard.log"
copy_if_exists "$cover_log"    "$LOG_DIR/guardian.log"

# ── Versions + payload info (no secrets) ────────────────────────────────────
{
	echo "=== slsteam-moon log bundle ==="
	echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date)"
	echo "uname: $(uname -a 2>/dev/null || true)"
	echo ""
	echo "=== payload ==="
	ls -l "$HOME/.local/share/SLSsteam/" 2>/dev/null || true
	echo ""
	echo "=== versions ==="
	cat "$HOME/.local/share/Lumen/versions.json" 2>/dev/null || true
} > "$LOG_DIR/bundle-info.txt"

# ── Scrub sensitive strings from text logs ──────────────────────────────────
HOME_ESC="$(printf '%s' "$HOME" | sed 's/[&/\]/\\&/g')"
for f in "$LOG_DIR"/*.txt; do
	[ -f "$f" ] || continue
	sed -i "s|$HOME_ESC|~|g" "$f" 2>/dev/null || true
	sed -i '/SLS_CARECA_SESSION/d; /LD_AUDIT/d; /STEAM_FORCE_CLIENT/d; /XDG_CONFIG_HOME.*SLSsteam-careca/d' "$f" 2>/dev/null || true
done
for f in "$LOG_DIR"/*.log; do
	[ -f "$f" ] || continue
	sed -i "s|$HOME_ESC|~|g" "$f" 2>/dev/null || true
	sed -i '/SLS_CARECA_SESSION/d; /LD_AUDIT/d; /STEAM_FORCE_CLIENT/d; /XDG_CONFIG_HOME.*SLSsteam-careca/d' "$f" 2>/dev/null || true
done

# ── Package ─────────────────────────────────────────────────────────────────
rm -f "$OUT"
(cd "$TMP" && zip -qr "$OUT" logs bundle-info.txt 2>/dev/null || true)

if [ -f "$OUT" ] && [ -s "$OUT" ]; then
	printf '%s\n' "$OUT"
else
	printf '' >&2
	exit 1
fi
