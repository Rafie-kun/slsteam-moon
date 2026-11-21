#!/bin/bash
# Controlled x86_64 load test (see tools/loadtest/load_client.c).  Touches nothing in the
# real Steam directory (modules are only read) and nothing under the real HOME: HOME and
# the XDG directories are a temporary tree, the session bus is unset, and the test config
# turns every networked or desktop-visible feature off.
#
#   scripts/loadtest-x64.sh [steam-root]     (default: ~/.local/share/Steam)
set -u
cd "$(dirname "$0")/.."
ROOT="${1:-$HOME/.local/share/Steam}"
CLIENT="$ROOT/steamrt64/steamclient.so"
UI="$ROOT/steamrt64/steamui.so"
LIB="$PWD/bin/SLSsteam.so"

for f in "$CLIENT" "$UI" "$LIB"; do
	[ -f "$f" ] || { echo "missing: $f" >&2; exit 2; }
done
class() { od -An -tx1 -j4 -N1 "$1" | tr -d ' \n'; }
for f in "$CLIENT" "$UI" "$LIB"; do
	[ "$(class "$f")" = 02 ] || { echo "not a 64-bit ELF: $f" >&2; exit 2; }
done

T="$(mktemp -d /tmp/slsm-loadtest.XXXXXX)"
trap 'echo "test tree kept in $T"' EXIT
mkdir -p "$T/home/.config/SLSsteam" "$T/home/.local/share" "$T/runtime" "$T/cc"
chmod 700 "$T/runtime"
cat > "$T/home/.config/SLSsteam/config.yaml" <<'YAML'
Notifications: no
NotifyInit: no
Donate:
  Enabled: no
AsyncProvision: no
AutoUpdateApps: no
PatternCache: no
API: no
SafeMode: no
WarnHashMissmatch: no
LogLevel: 0
ExtendedLogging: yes
YAML

# setup() only acts in a process named "steam"; the harness must carry that name.
mkdir -p "$T/bin"
gcc -O1 -Wall -o "$T/bin/steam" tools/loadtest/load_client.c -ldl || exit 2

echo "== client:  $CLIENT ($(sha256sum "$CLIENT" | cut -c1-12))"
echo "== library: $LIB"
echo "== HOME:    $T/home"
(
	ulimit -c 0
	env -i PATH=/usr/bin:/bin HOME="$T/home" XDG_CONFIG_HOME="$T/home/.config" \
	    XDG_DATA_HOME="$T/home/.local/share" XDG_CACHE_HOME="$T/cc" \
	    XDG_RUNTIME_DIR="$T/runtime" LANG=C \
	    LD_LIBRARY_PATH="$ROOT/ubuntu12_64:$ROOT/steamrt64" \
	    LD_AUDIT="$LIB" \
	    SLSSTEAM_ATTESTATION_FILE="$T/attestation.jsonl" \
	    timeout 90 "$T/bin/steam" "$CLIENT" "$UI" "${SLSM_LOADTEST_SETTLE:-2}"
) > "$T/stdout.log" 2> "$T/stderr.log"
status=$?
echo "== exit status: $status (124 = timeout, 139 = SIGSEGV, 134 = SIGABRT)"
echo "== --- stderr ---"; cat "$T/stderr.log"
echo "== --- stdout ---"; cat "$T/stdout.log"
echo "== --- attestation (locator events) ---"; cat "$T/attestation.jsonl" 2>/dev/null | cut -c1-200
echo "== --- SLSsteam log ---"; cat "$T/home/.SLSsteam.log" 2>/dev/null | cut -c1-220 | head -80
echo "== --- files written under the test HOME ---"
(cd "$T/home" && find . -type f -printf '%p (%s bytes)\n' | head -20)
exit "$status"
