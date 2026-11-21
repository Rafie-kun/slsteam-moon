#!/usr/bin/env bash
# Portable build implementation. Invoked by scripts/build.sh --portable.
# Builds inside an Ubuntu 22.04 container. The native helper is then checked
# separately so it cannot require any glibc symbol newer than GLIBC_2.34.
# Falls back to a host build when no container runtime is available (CI).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

# Pick a container runtime: prefer rootless podman, fall back to docker.
runtime=""
for c in podman docker; do
	if command -v "$c" >/dev/null 2>&1; then runtime="$c"; break; fi
done

if [ -n "$runtime" ]; then
	echo "==> portable build (using $runtime, image slsteam-moon-builder)"

	# Build (or reuse) the builder image. Tagging by name only, the layer cache
	# inside the runtime keeps rebuilds cheap.
	"$runtime" build -f scripts/Dockerfile -t slsteam-moon-builder . >&2

	# Run the build inside the container with the repo bind-mounted, so the
	# resulting bin/ lands on the host without needing a docker cp.
	"$runtime" run --rm \
		-v "$PWD:/build:Z" \
		-w /build \
		slsteam-moon-builder \
		bash -c 'make clean && make'

	scripts/check-pattern-refresh-abi.sh bin/pattern-refresh
else
	echo "==> no container runtime found; falling back to host build"
	make clean
	make
fi

echo "==> built: bin/SLSsteam.so, bin/library-inject.so, bin/pattern-refresh"
