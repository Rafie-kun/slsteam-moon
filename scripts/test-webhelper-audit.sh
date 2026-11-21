#!/bin/bash
# Régression x86_64 : un processus nommé steamwebhelper sous LD_AUDIT doit démarrer et
# finir normalement (setup() y retire LD_AUDIT avant que le journal n'existe : segfault le
# 2026-11-13) ; un processus quelconque doit aussi être laissé tranquille.
set -u
cd "$(dirname "$0")/.."
LIB="$PWD/bin/SLSsteam.so"
[ -f "$LIB" ] || { echo "missing: $LIB" >&2; exit 2; }
T="$(mktemp -d /tmp/slsm-webhelper.XXXXXX)"; trap 'rm -rf "$T"' EXIT
cp "$(command -v printenv)" "$T/steamwebhelper"
cp "$(command -v printenv)" "$T/otherproc"
out="$(HOME="$T" LD_AUDIT="$LIB" "$T/steamwebhelper" LD_AUDIT)"; rc=$?
# printenv sort 1 si la variable est absente : c'est le résultat attendu (retirée)
[ $rc -eq 1 ] && [ -z "$out" ] || { echo "FAIL steamwebhelper: rc=$rc out='$out'"; exit 1; }
out="$(HOME="$T" LD_AUDIT="$LIB" "$T/otherproc" LD_AUDIT)"; rc=$?
[ $rc -eq 0 ] && [ "$out" = "$LIB" ] || { echo "FAIL autre processus: rc=$rc out='$out'"; exit 1; }
echo "PASS test-webhelper-audit"
