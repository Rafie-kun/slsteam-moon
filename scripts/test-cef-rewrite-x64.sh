#!/bin/bash
# x86_64 : un processus nommé `steam` qui exécute le webhelper avec le port CEF figé 8080
# doit voir l'argument réécrit par la_symbind64 et le port publié pour Lumen.
# (Sur x86_64 le loader n'appelle que la_symbind64 : sans lui, le port reste 8080.)
# Le processus modélise le client (STEAM_STEAMRT_RECURSION_GUARD) : le lanceur externe ne fait
# pas de setup() (voir scripts/test-bootstrapper-x64.sh).
set -u
cd "$(dirname "$0")/.."
LIB="$PWD/bin/SLSsteam.so"
[ -f "$LIB" ] || { echo "missing: $LIB" >&2; exit 2; }
T="$(mktemp -d /tmp/slsm-cef.XXXXXX)"; trap 'rm -rf "$T"' EXIT
cat > "$T/t.c" <<'C'
#include <unistd.h>
int main(void) {
	char *a[] = {"echo", "sh -c exec steamwebhelper.sh --remote-debugging-port=8080 -lang=en", 0};
	execv("/bin/echo", a);
	return 1;
}
C
gcc -O1 "$T/t.c" -o "$T/steam" || exit 2
mkdir -p "$T/home/.config/SLSsteam" "$T/home/.local/share" "$T/home/.cache"
printf 'Notifications: no\nNotifyInit: no\n' > "$T/home/.config/SLSsteam/config.yaml"
out="$(env -i PATH=/usr/bin:/bin HOME="$T/home" XDG_CONFIG_HOME="$T/home/.config" \
	XDG_DATA_HOME="$T/home/.local/share" XDG_CACHE_HOME="$T/home/.cache" \
	STEAM_STEAMRT_RECURSION_GUARD=1 LD_AUDIT="$LIB" "$T/steam")"
port="$(printf '%s' "$out" | sed -n 's/.*--remote-debugging-port=\([0-9]*\).*/\1/p')"
[ -n "$port" ] && [ "$port" != 8080 ] && [ "$port" -ge 1024 ] || { echo "FAIL: port=$port out='$out'"; exit 1; }
[ -s "$T/home/.local/share/Lumen/cef_port" ] || { echo "FAIL: cef_port not published"; exit 1; }
echo "PASS test-cef-rewrite-x64 (port $port)"
