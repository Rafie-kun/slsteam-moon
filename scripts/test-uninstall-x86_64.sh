#!/bin/bash
# uninstall-x86_64.sh against a stand-in upstream uninstaller, a throwaway HOME, a throwaway
# "/" (SLSM_FS_ROOT) and a sudo that only logs. Never touches the real HOME, /usr or Steam.
set -u
cd "$(dirname "$0")/.."
T="$(mktemp -d /tmp/slsm-uninst.XXXXXX)"; trap 'rm -rf "$T"' EXIT
fail=0; ok() { echo "  ok   - $1"; }; ko() { echo "  FAIL - $1"; fail=$((fail+1)); }

mkdir -p "$T/bin" "$T/stubs"
cat > "$T/bin/sudo" <<'SUDO'
#!/bin/sh
echo "sudo $*" >> "$SUDO_LOG"
exec "$@"
SUDO
chmod +x "$T/bin/sudo"

# Stand-in upstream uninstallers (they only act inside $HOME).
cat > "$T/stubs/full.sh" <<'STUB'
echo "KEEP_MILLENNIUM=${LUATOOLS_MOON_KEEP_MILLENNIUM:-unset}" > "$STUB_OUT"
rm -rf -- "$HOME/.local/share/SLSsteam" "$HOME/.local/share/Lumen" "$HOME/.config/SLSsteam" \
	"$HOME/.local/state/slsteam-moon" "$HOME/.config/systemd/user/slsteam-desktop-guardian.path"
sed -i '/SLSsteam/d' "$HOME/.zshrc" "$HOME/.profile" 2>/dev/null
true
STUB
cat > "$T/stubs/leaves-launcher.sh" <<'STUB'
. "$(dirname "$0")/full.sh" >/dev/null
mkdir -p "$HOME/.local/share/SLSsteam/system-launcher-backup/usr/bin"
cp "$ORIG_LAUNCHER" "$HOME/.local/share/SLSsteam/system-launcher-backup/usr/bin/steam.orig"
true
STUB
cat > "$T/stubs/noop.sh" <<'STUB'
echo "KEEP_MILLENNIUM=${LUATOOLS_MOON_KEEP_MILLENNIUM:-unset}" > "$STUB_OUT"
touch "$HOME/upstream-ran"
exit 0
STUB
cat > "$T/stubs/fails.sh" <<'STUB'
touch "$HOME/upstream-ran"
exit 3
STUB

ORIG="$T/orig-launcher"; printf '#!/bin/sh\nexec /usr/lib/steam/steam "$@"\n' > "$ORIG"
SHIM="$T/shim-launcher"; printf '#!/bin/sh\n# slsteam-moon system launcher shim\nexec x\n' > "$SHIM"

mkhome() {
	rm -rf "$T/home" "$T/root"; mkdir -p "$T/home/.local/share/SLSsteam/path" "$T/home/.local/share/Lumen" \
		"$T/home/.config/SLSsteam" "$T/home/.local/state/slsteam-moon" "$T/home/.config/systemd/user" \
		"$T/home/.local/share/Steam/config/stplug-in" "$T/root/usr/bin"
	echo wrapper > "$T/home/.local/share/SLSsteam/path/steam"
	echo "DisableCloud: yes" > "$T/home/.config/SLSsteam/config.yaml"
	echo "1245620 lua" > "$T/home/.local/share/Steam/config/stplug-in/1245620.lua"
	: > "$T/home/.config/systemd/user/slsteam-desktop-guardian.path"
	printf '%s\n' '# SLSsteam: Add wrapper to PATH' 'export PATH="$HOME/.local/share/SLSsteam/path:$PATH"' > "$T/home/.zshrc"
	cp "$T/home/.zshrc" "$T/home/.profile"
	: > "$T/home/.SLSsteam.log"; : > "$T/home/.lumen.log"
	cp "$ORIG" "$T/root/usr/bin/steam"
}
: > "$T/sudo.log"
run() { # $1 = stub  rest = extra env
	local stub="$1"; shift
	env -i PATH="$T/bin:/usr/bin:/bin" HOME="$T/home" SUDO_LOG="$T/sudo.log" STUB_OUT="$T/stub.env" \
		ORIG_LAUNCHER="$ORIG" LUATOOLS_UNINSTALL_FILE="$T/stubs/$stub" SLSM_FS_ROOT="$T/root" "$@" \
		bash uninstall-x86_64.sh 2>&1 < /dev/null
}

# 1. clean run: everything is removed, config backed up, Millennium protected, user data kept
mkhome; out="$(run full.sh; echo "rc=$?")"
echo "$out" | grep -q 'rc=0' && ok "a complete uninstall exits 0" || ko "complete uninstall failed: $out"
bk="$(ls -d "$T"/home/slsteam-moon-backup-* 2>/dev/null | head -1)"
[ -n "$bk" ] && [ "$(cat "$bk/config-SLSsteam/config.yaml" 2>/dev/null)" = "DisableCloud: yes" ] \
	&& ok "~/.config/SLSsteam is backed up before the upstream script runs" || ko "no backup of the configuration"
grep -q 'KEEP_MILLENNIUM=1' "$T/stub.env" && ok "Millennium is protected (LUATOOLS_MOON_KEEP_MILLENNIUM=1)" || ko "Millennium not protected"
[ -f "$T/home/.local/share/Steam/config/stplug-in/1245620.lua" ] && [ -e "$T/home/.SLSsteam.log" ] && [ -e "$T/home/.lumen.log" ] \
	&& ok "game scripts and files the script does not own are left alone" || ko "user data was removed"
echo "$out" | grep -q "kept in $bk" && ok "the backup location is printed" || ko "backup location not printed"

# 2. nothing removed: the result is checked and reported
mkhome; out="$(run noop.sh; echo "rc=$?")"
echo "$out" | grep -q 'rc=1' && echo "$out" | grep -q 'still present: .*/.local/share/SLSsteam' \
	&& echo "$out" | grep -q 'still puts the slsteam-moon wrapper in PATH' && ok "leftovers make it exit 1 and are listed" || ko "leftovers not reported: $out"

# 3. upstream failure is reported even if the files are gone
mkhome; out="$(run fails.sh; echo "rc=$?")"
echo "$out" | grep -q 'exited with 3' && echo "$out" | grep -q 'rc=1' && ok "an upstream failure is reported (exit 1)" || ko "upstream failure hidden: $out"

# 4. the root-owned launcher backup: removed only when the live launcher is the original again
mkhome; : > "$T/sudo.log"; out="$(run leaves-launcher.sh; echo "rc=$?")"
echo "$out" | grep -q 'Removing the leftover launcher backup' && echo "$out" | grep -q 'rc=0' && [ ! -e "$T/home/.local/share/SLSsteam" ] \
	&& ok "launcher backup identical to the live launcher: removed" || ko "leftover not removed: $out"
mkhome; cp "$SHIM" "$T/root/usr/bin/steam"; out="$(run leaves-launcher.sh; echo "rc=$?")"
echo "$out" | grep -q 'rc=1' && echo "$out" | grep -q 'still the slsteam-moon shim' && [ -f "$T/home/.local/share/SLSsteam/system-launcher-backup/usr/bin/steam.orig" ] \
	&& ok "live launcher still the shim: backup kept, restore command printed" || ko "backup deleted while the shim is live: $out"
mkhome; echo different > "$T/root/usr/bin/steam"; out="$(run leaves-launcher.sh; echo "rc=$?")"
echo "$out" | grep -q 'differs from its backup' && [ -f "$T/home/.local/share/SLSsteam/system-launcher-backup/usr/bin/steam.orig" ] \
	&& ok "live launcher differs: backup kept" || ko "differing launcher not handled: $out"
mkhome; rm "$T/root/usr/bin/steam"; out="$(run leaves-launcher.sh; echo "rc=$?")"
echo "$out" | grep -q 'only copy of the original launcher' && [ -f "$T/home/.local/share/SLSsteam/system-launcher-backup/usr/bin/steam.orig" ] \
	&& ok "live launcher missing: backup kept" || ko "missing launcher not handled: $out"
mkhome; cat > "$T/stubs/leaves-extra.sh" <<'STUB'
. "$(dirname "$0")/leaves-launcher.sh" >/dev/null
echo precious > "$HOME/.local/share/SLSsteam/user-note"
STUB
out="$(run leaves-extra.sh; echo "rc=$?")"
echo "$out" | grep -q 'other than the launcher backup' && [ -f "$T/home/.local/share/SLSsteam/user-note" ] \
	&& ok "an unknown file next to the backup is never deleted" || ko "unknown file deleted: $out"

# 4b. On a terminal, `rm -r` asks "remove write-protected file?" for a read-only file; with its
# stderr hidden that looked like a hang on a real run. stdin must be a terminal to see it.
if command -v python3 >/dev/null 2>&1; then
	cat > "$T/stubs/leaves-readonly.sh" <<'STUB'
. "$(dirname "$0")/leaves-launcher.sh" >/dev/null
chmod 0444 "$HOME/.local/share/SLSsteam/system-launcher-backup/usr/bin/steam.orig"
STUB
	mkhome; out="$(env -i PATH="$T/bin:/usr/bin:/bin" HOME="$T/home" SUDO_LOG="$T/sudo.log" STUB_OUT="$T/stub.env" \
		ORIG_LAUNCHER="$ORIG" LUATOOLS_UNINSTALL_FILE="$T/stubs/leaves-readonly.sh" SLSM_FS_ROOT="$T/root" \
		python3 -c '
import os, pty, subprocess, sys
m, sl = pty.openpty()
p = subprocess.Popen(["bash", "uninstall-x86_64.sh"], stdin=sl, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, start_new_session=True)
os.close(sl)
try:
    out, _ = p.communicate(timeout=20)
except subprocess.TimeoutExpired:
    # the whole group: a child (rm) waiting for its answer would keep the pipe open forever
    os.killpg(p.pid, 9); out, _ = p.communicate()
    print(out.decode(errors="replace")); print("TIMEOUT"); sys.exit(124)
print(out.decode(errors="replace")); print("rc=%d" % p.returncode)
' 2>&1)"
	echo "$out" | grep -q 'TIMEOUT' && ko "a read-only leftover makes the uninstaller wait for a hidden answer on a terminal" \
		|| { echo "$out" | grep -q 'rc=0' && [ ! -e "$T/home/.local/share/SLSsteam" ] \
			&& ok "a read-only launcher backup is removed without waiting on a terminal" || ko "read-only leftover not removed: $out"; }
else
	echo "  skip - python3 missing: the terminal case is not tested"
fi

# 5. nothing is touched when the upstream script cannot be trusted
mkhome; out="$(run noop.sh LUATOOLS_UNINSTALL_SHA256=deadbeef; echo "rc=$?")"
echo "$out" | grep -q 'does not match' && [ ! -e "$T/home/upstream-ran" ] && ok "a pinned sha256 mismatch stops before anything runs" || ko "sha pin ignored: $out"
mkhome; out="$(run does-not-exist.sh; echo "rc=$?")"
echo "$out" | grep -q 'not found' && [ -d "$T/home/.config/SLSsteam" ] && ok "a missing upstream script stops, nothing removed" || ko "missing script not handled: $out"

# 6. a HOME that is not a user home is refused
for bad in / /tmp ""; do
	mkhome; out="$(env -i PATH="$T/bin:/usr/bin:/bin" HOME="$bad" LUATOOLS_UNINSTALL_FILE="$T/stubs/noop.sh" STUB_OUT="$T/stub.env" bash uninstall-x86_64.sh 2>&1 < /dev/null; echo "rc=$?")"
	echo "$out" | grep -q 'does not look like a user home' && ok "HOME='$bad' is refused" || ko "HOME='$bad' accepted: $out"
done
[ ! -e "$T/home/upstream-ran" ] && ok "the upstream script never ran for those" || ko "upstream ran with a bad HOME"

# 7. SLSM_NO_BACKUP skips the copy
mkhome; run full.sh SLSM_NO_BACKUP=1 >/dev/null; ls -d "$T"/home/slsteam-moon-backup-* >/dev/null 2>&1 && ko "backup made despite SLSM_NO_BACKUP=1" || ok "SLSM_NO_BACKUP=1 skips the backup"

echo "== total: $fail failed =="
[ "$fail" -eq 0 ]
