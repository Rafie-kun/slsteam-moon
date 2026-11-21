#!/bin/bash
# Real-client trial of the x86_64 build, on a COPY of the Steam client and with no network.
#
# What it does, and does not do:
#  - copies the client directories of a Steam root (not config/, steamapps/, userdata/,
#    appcache/, logs/, depotcache/: no account, no games) into a private directory;
#  - starts that copy's steam.sh with STEAM_FORCE_CLIENT=steamrt64, LD_AUDIT=bin/SLSsteam.so,
#    an isolated HOME/XDG tree and private session bus, inside a network-less user namespace (unshare -rn), on a
#    nested X server (Xephyr window on your screen), killed after SLSM_TRIAL_SECS (default 90);
#  - prints the SLSsteam log and the client's logs.
# The real Steam root and the real HOME are only read.  Nothing is installed.
#
#   scripts/trial-real-client-x64.sh [steam-root]      SLSM_TRIAL_KEEP=1 keeps the copy
set -u
cd "$(dirname "$0")/.."
ROOT="${1:-$HOME/.local/share/Steam}"
LIB="$PWD/bin/SLSsteam.so"
SECS="${SLSM_TRIAL_SECS:-90}"
DEST="${SLSM_TRIAL_DIR:-$HOME/.cache/slsm-trial}"

die() { echo "error: $*" >&2; exit 2; }
[ -f "$LIB" ] || die "missing $LIB (make)"
[ "$(od -An -tx1 -j4 -N1 "$LIB" | tr -d ' \n')" = 02 ] || die "$LIB is not a 64-bit ELF"
[ -x "$ROOT/steamrt64/steam" ] && [ -f "$ROOT/steamrt64/steamclient.so" ] || die "no SteamRT 64-bit client in $ROOT"
[ -f "$ROOT/steam.sh" ] || die "no steam.sh in $ROOT"
command -v unshare >/dev/null && command -v Xephyr >/dev/null && command -v dbus-run-session >/dev/null \
	|| die "needs unshare, Xephyr and dbus-run-session"
unshare -rn true 2>/dev/null || die "unprivileged network namespaces are unavailable"
[ -n "${DISPLAY:-}" ] || die "needs a running X/XWayland display for the Xephyr window"
pgrep -x steam >/dev/null 2>&1 && die "a Steam is already running; close it first"

case "$DEST" in "$HOME"/.cache/slsm-trial*|/tmp/slsm-trial*) ;; *) die "refusing unusual trial dir $DEST" ;; esac
rm -rf "$DEST"; mkdir -p "$DEST/root/logs" "$DEST/home/.config/SLSsteam" "$DEST/home/.local/share/SLSsteam" \
                          "$DEST/runtime" "$DEST/cache" "$DEST/out"
# Same place as a real install: the SteamRT container shares HOME, so LD_AUDIT resolves
# inside it; a path elsewhere under /home would not necessarily be visible there.
cp "$LIB" "$DEST/home/.local/share/SLSsteam/SLSsteam.so"
AUDIT_LIB="$DEST/home/.local/share/SLSsteam/SLSsteam.so"
chmod 700 "$DEST/runtime"

echo "== copying client directories from $ROOT"
for d in steamrt64 ubuntu12_32 ubuntu12_64 linux32 linux64 package steamui clientui public resource \
         graphics friends tenfoot bin controller_base legacycompat; do
	[ -e "$ROOT/$d" ] && cp -a "$ROOT/$d" "$DEST/root/$d"
done
for f in steam.sh steam steam_subscriber_agreement.txt bootstrap.tar.xz; do
	[ -e "$ROOT/$f" ] && cp -a "$ROOT/$f" "$DEST/root/$f"
done
rm -f "$DEST/root/.steam-disable-steamrt64-client"      # the trial must run the 64-bit client
echo "== copy size: $(du -sh "$DEST/root" | cut -f1); client sha256: $(sha256sum "$DEST/root/steamrt64/steamclient.so" | cut -c1-12)"

cat > "$DEST/home/.config/SLSsteam/config.yaml" <<'YAML'
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

XNUM=77
while [ -e "/tmp/.X11-unix/X$XNUM" ]; do XNUM=$((XNUM + 1)); done
Xephyr ":$XNUM" -screen 1280x800 -ac -br >"$DEST/out/xephyr.log" 2>&1 &
XPID=$!
for _ in $(seq 1 50); do [ -S "/tmp/.X11-unix/X$XNUM" ] && break; sleep 0.1; done
[ -S "/tmp/.X11-unix/X$XNUM" ] || { kill "$XPID" 2>/dev/null; die "Xephyr did not start (see $DEST/out/xephyr.log)"; }

echo "== launching the copy for ${SECS}s on :$XNUM (no network, HOME=$DEST/home)"
(
	cd "$DEST/root" || exit 2
	ulimit -c 0
	env -i PATH=/usr/bin:/bin HOME="$DEST/home" XDG_CONFIG_HOME="$DEST/home/.config" \
	    XDG_DATA_HOME="$DEST/home/.local/share" XDG_CACHE_HOME="$DEST/cache" \
	    XDG_RUNTIME_DIR="$DEST/runtime" DISPLAY=":$XNUM" LANG=C.UTF-8 \
	    STEAM_FORCE_CLIENT=steamrt64 LD_AUDIT="$AUDIT_LIB" \
	    unshare -rn timeout --signal=TERM --kill-after=10 "$SECS" \
	        dbus-run-session -- ./steam.sh -silent -nobootstrapupdate -skipinitialbootstrap
) >"$DEST/out/steam.stdout" 2>"$DEST/out/steam.stderr"
status=$?
kill "$XPID" 2>/dev/null; wait "$XPID" 2>/dev/null
pkill -u "$USER" -f "$DEST/root" 2>/dev/null     # anything the copy left behind

echo "== exit status: $status (124 = ran until the timeout, 139 = SIGSEGV, 134 = SIGABRT)"
echo "== --- steam.sh stdout/stderr (tail) ---"
tail -n 25 "$DEST/out/steam.stdout" "$DEST/out/steam.stderr" 2>/dev/null | cut -c1-200
echo "== --- SLSsteam log: failures, hooks, first calls ---"
if [ -f "$DEST/home/.SLSsteam.log" ]; then
	wc -l < "$DEST/home/.SLSsteam.log" | sed 's/^/lines: /'
	grep -n "Required\|Failed\|trampoline\|crash\|Hooks::setup\|Detour hooked\|loading in\|steamclient.so loaded\|Abort" \
	     "$DEST/home/.SLSsteam.log" | cut -c1-190 | head -60
else
	echo "(no ~/.SLSsteam.log: SLSsteam was not loaded in a process named steam)"
fi
echo "== --- client logs written in the copy ---"
ls -la "$DEST/root/logs" 2>/dev/null | head -20
echo "== trial tree: $DEST"
[ "${SLSM_TRIAL_KEEP:-0}" = 1 ] || echo "   (delete with: rm -rf '$DEST')"
exit "$status"
