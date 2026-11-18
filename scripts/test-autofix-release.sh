#!/usr/bin/env bash
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AUTOFIX_LIB_ONLY=1 source "$HERE/autofix.sh"

pass=0
fail=0
check() {
	if [ "$2" = "$3" ]; then
		pass=$((pass + 1))
	else
		fail=$((fail + 1))
		printf 'FAIL: %s (got %s, want %s)\n' "$1" "$2" "$3"
	fi
}

# URLs mirror GitHub's real shape: the asset filename is part of the path, which
# the grep fallback relies on. The sidecar is listed before the archive because
# picking it instead of the archive is the failure this test guards against.
releases='[
  {"tag_name":"v2.9","draft":false,"prerelease":false,"assets":[
    {"name":"slsteam-moon-linux-2.9-lumen.zip.uploading","browser_download_url":"https://github.invalid/releases/download/v2.9/temp"},
    {"name":"slsteam-moon-linux-2.9.zip.sha256","browser_download_url":"https://github.invalid/releases/download/v2.9/slsteam-moon-linux-2.9.zip.sha256"}
  ]},
  {"tag_name":"v2.8","draft":false,"prerelease":false,"assets":[
    {"name":"slsteam-moon-linux-2.8.zip.sha256","browser_download_url":"https://github.invalid/releases/download/v2.8/slsteam-moon-linux-2.8.zip.sha256"},
    {"name":"slsteam-moon-linux-2.8.zip","browser_download_url":"https://github.invalid/releases/download/v2.8/slsteam-moon-linux-2.8.zip"}
  ]}
]'
check "the published asset name is found and its sidecar is skipped" \
	"$(resolve_github_asset "$releases")" \
	"https://github.invalid/releases/download/v2.8/slsteam-moon-linux-2.8.zip"

manifest='{
  "schema": 1,
  "components": {
    "slsteam-moon": {
      "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "url": "https://cdn.jsdelivr.net/gh/Rafie-kun/jsdelivr@0123456789012345678901234567890123456789/releases/slsteam-moon/v2.9/hash/slsteam-moon-linux-2.9-lumen.zip"
    }
  }
}'
entry="$(resolve_mirror_entry "$manifest")"
IFS=$'\t' read -r mirror_url mirror_sha <<< "$entry"
check "mirror URL parsed" "$mirror_url" \
	"https://cdn.jsdelivr.net/gh/Rafie-kun/jsdelivr@0123456789012345678901234567890123456789/releases/slsteam-moon/v2.9/hash/slsteam-moon-linux-2.9-lumen.zip"
check "mirror digest parsed" "$mirror_sha" \
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
check "commit-pinned mirror entry accepted" \
	"$(valid_mirror_entry "$mirror_url" "$mirror_sha" && echo yes || echo no)" "yes"
check "branch-pinned mirror entry rejected" \
	"$(valid_mirror_entry "https://cdn.jsdelivr.net/gh/Rafie-kun/jsdelivr@main/file.zip" "$mirror_sha" && echo yes || echo no)" "no"

printf 'autofix release: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
