#!/bin/bash
# x86_64 : the steamrt64 `steam` runs twice, the outer bootstrapper and the client it re-launches
# in the runtime (only that one carries STEAM_STEAMRT_RECURSION_GUARD). The bootstrapper must
#   - not run setup() (duplicate work, a config watcher on descriptors it is about to close),
#   - keep LD_AUDIT for the client it launches,
#   - and never leave a thread spinning, with or without setup() having started a watcher.
# The client must still run setup(), and its watcher must stop, not spin, when its inotify
# descriptor is gone.
#
#   SLSM_TEST_LIB=<other SLSsteam.so> to run the same checks against another build.
set -u
cd "$(dirname "$0")/.."
LIB="${SLSM_TEST_LIB:-$PWD/bin/SLSsteam.so}"
[ -f "$LIB" ] || { echo "missing: $LIB" >&2; exit 2; }
T="$(mktemp -d /tmp/slsm-boot.XXXXXX)"; trap 'rm -rf "$T"' EXIT
g++ -O1 -std=c++20 tools/test_steam_stub.cpp -o "$T/steam" || exit 2
LIMIT_MS=500
fail=0; ok() { echo "  ok   - $1"; }; ko() { echo "  FAIL - $1"; fail=$((fail+1)); }

fresh_home() {
	rm -rf "$T/home"; mkdir -p "$T/home/.config/SLSsteam" "$T/home/.local/share" "$T/home/.cache"
	printf 'Notifications: no\nNotifyInit: no\n' > "$T/home/.config/SLSsteam/config.yaml"
}
run() { # extra env assignments..., then the stub arguments after "--"
	local envs=(); while [ "$1" != "--" ]; do envs+=("$1"); shift; done; shift
	env -i PATH=/usr/bin:/bin HOME="$T/home" XDG_CONFIG_HOME="$T/home/.config" \
		XDG_DATA_HOME="$T/home/.local/share" XDG_CACHE_HOME="$T/home/.cache" \
		"${envs[@]}" LD_AUDIT="$LIB" "$T/steam" "$@" 2>&1
}
log() { cat "$T/home/.SLSsteam.log" 2>/dev/null; }
cpu_of() { printf '%s' "$1" | sed -n 's/.*cpu_ms=\([0-9]*\).*/\1/p' | tail -1; }

# 1. the client (guard set): setup() runs, and a closed descriptor must not make the watcher spin
fresh_home
out="$(run STEAM_STEAMRT_RECURSION_GUARD=1 -- spin)"; cpu="$(cpu_of "$out")"
case "$(log)" in *"CEF: remote-debugging port"*) ok "client: setup() ran" ;; *) ko "client: setup() did not run: $(log | tail -3)" ;; esac
[ -n "$cpu" ] && [ "$cpu" -lt "$LIMIT_MS" ] && ok "client: no thread spins after its descriptors are closed (${cpu} ms of CPU in 2.5 s)" \
	|| ko "client: spinning after its descriptors were closed (cpu_ms='${cpu}', limit ${LIMIT_MS}): $out"

# 2. the bootstrapper (no guard): no setup(), LD_AUDIT kept
fresh_home
out="$(run -- env)"
[ "$out" = "$LIB" ] && ok "bootstrapper: LD_AUDIT is kept for the client it launches" || ko "bootstrapper: LD_AUDIT lost: '$out'"
case "$(log)" in *"steamrt64 bootstrapper"*) ok "bootstrapper: setup() skipped and said so" ;; *) ko "bootstrapper: no skip line: $(log | tail -3)" ;; esac
case "$(log)" in *"CEF:"*) ko "bootstrapper: setup() ran" ;; *) ok "bootstrapper: setup() did not run" ;; esac
[ ! -e "$T/home/.local/share/Lumen/cef_port" ] && ok "bootstrapper: published nothing for Lumen" || ko "bootstrapper: published a CEF port"

# 3. the bootstrapper closes its descriptors and waits: it must not burn CPU either
fresh_home
out="$(run -- spin)"; cpu="$(cpu_of "$out")"
[ -n "$cpu" ] && [ "$cpu" -lt "$LIMIT_MS" ] && ok "bootstrapper: idle while waiting (${cpu} ms of CPU in 2.5 s)" \
	|| ko "bootstrapper: spinning while waiting (cpu_ms='${cpu}', limit ${LIMIT_MS}): $out"

echo "== total: $fail failed =="
[ "$fail" -eq 0 ]
