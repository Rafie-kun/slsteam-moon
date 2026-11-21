#!/bin/bash
# Client-layout detection of setup.sh (installer copy and the copy inside the generated
# wrapper) and the ELF-class helper.  Fixture Steam roots only: nothing is installed and
# no Steam file is touched.
set -u
cd "$(dirname "$0")/.."
fail=0
ok()  { printf 'ok:   %s\n' "$1"; }
bad() { printf 'FAIL: %s\n' "$1"; fail=1; }

# Pull a function definition out of setup.sh by name; $2 selects the occurrence
# (1 = installer, 2 = inside the wrapper heredoc).
extract() {
	awk -v name="$1" -v want="$2" '
		$0 ~ "^"name"\\(\\) \\{" { n++; if (n == want) grab = 1 }
		grab { print }
		grab && /^}/ { grab = 0 }
	' setup.sh
}

extract slsm_client_dir 1 > /tmp/slsm_dir_installer.sh
extract slsm_client_dir 2 > /tmp/slsm_dir_wrapper.sh
[ -s /tmp/slsm_dir_installer.sh ] && ok "installer copy found" || bad "installer copy missing"
[ -s /tmp/slsm_dir_wrapper.sh ] && ok "wrapper copy found" || bad "wrapper copy missing"
# The comment above each copy differs; the code must not.
if diff <(grep -v '^[[:space:]]*#' /tmp/slsm_dir_installer.sh) <(grep -v '^[[:space:]]*#' /tmp/slsm_dir_wrapper.sh) >/dev/null; then
	ok "both copies are identical"
else
	bad "installer and wrapper copies of slsm_client_dir differ"
fi

extract slsm_elf_class 1 > /tmp/slsm_elf_class.sh
# shellcheck disable=SC1091
. /tmp/slsm_dir_installer.sh
. /tmp/slsm_elf_class.sh

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
mk64() {  # root with a SteamRT 64-bit client
	mkdir -p "$1/steamrt64" "$1/ubuntu12_32"
	printf '#!/bin/sh\n' > "$1/steamrt64/steam"; chmod +x "$1/steamrt64/steam"
	: > "$1/steamrt64/steamclient.so"; : > "$1/ubuntu12_32/steamclient.so"
}
expect() {  # description expected root
	got="$(slsm_client_dir "$3")"
	[ "$got" = "$3/$2" ] && ok "$1 -> $2" || bad "$1: got '$got', wanted '$3/$2'"
}

unset STEAM_FORCE_CLIENT
mk64 "$T/a";                          expect "64-bit client present" steamrt64 "$T/a"
mk64 "$T/b"; : > "$T/b/.steam-disable-steamrt64-client"
                                      expect "disable sentinel" ubuntu12_32 "$T/b"
STEAM_FORCE_CLIENT=steamrt64          expect "force steamrt64 beats the sentinel" steamrt64 "$T/b"
STEAM_FORCE_CLIENT=scout              expect "force scout" ubuntu12_32 "$T/a"
unset STEAM_FORCE_CLIENT
mkdir -p "$T/c/ubuntu12_32";          expect "legacy-only install" ubuntu12_32 "$T/c"
mkdir -p "$T/d/steamrt64"; : > "$T/d/steamrt64/steamclient.so"
                                      expect "steamrt64 without an executable steam" ubuntu12_32 "$T/d"

printf '\177ELF\002' > "$T/c64"; printf '\177ELF\001' > "$T/c32"; printf 'not elf' > "$T/cx"
[ "$(slsm_elf_class "$T/c64")" = 64 ] && ok "ELF64 detected" || bad "ELF64 not detected"
[ "$(slsm_elf_class "$T/c32")" = 32 ] && ok "ELF32 detected" || bad "ELF32 not detected"
[ "$(slsm_elf_class "$T/cx")" = '?' ] && ok "non-ELF is unknown" || bad "non-ELF not unknown"
[ "$(slsm_elf_class "$T/missing")" = '?' ] && ok "missing file is unknown" || bad "missing file not unknown"
[ "$(slsm_elf_class bin/SLSsteam.so 2>/dev/null)" != 32 ] && ok "built SLSsteam.so is not 32-bit" || bad "built SLSsteam.so is 32-bit"

exit $fail
