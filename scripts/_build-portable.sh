#!/usr/bin/env bash
# Portable build implementation. Invoked by scripts/build.sh --portable.
# Builds inside an Ubuntu 22.04 container. The native helper is then checked
# separately so it cannot require any glibc symbol newer than GLIBC_2.34.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

# Pick a container runtime: prefer rootless podman, fall back to docker.
runtime=""
for c in podman docker; do
	if command -v "$c" >/dev/null 2>&1; then runtime="$c"; break; fi
done
if [ -z "$runtime" ]; then
	cat >&2 <<-EOF
	No container runtime found.

	Install one of these and try again:
	  Podman (rootless, recommended):
	    Ubuntu/Debian: sudo apt install podman
	    Fedora:        sudo dnf install podman
	    Arch:          sudo pacman -S podman
	  Docker:
	    Ubuntu/Debian: sudo apt install docker.io && sudo systemctl enable --now docker
	    Fedora:        sudo dnf install docker
	    Arch:          sudo pacman -S docker
	    (then: sudo usermod -aG docker \$USER && newgrp docker)

	Or build directly on your host:  scripts/build.sh --host
	EOF
	exit 1
fi

# Target class: ARCH (default: the host's) selects the 64-bit builder image or the
# 32-bit one, like the Makefile does.
ARCH="${ARCH:-$(uname -m)}"
if [ "$ARCH" = "x86_64" ]; then
	DOCKERFILE="scripts/Dockerfile.x86_64"; IMAGE="slsteam-moon-builder-x86_64"
else
	DOCKERFILE="scripts/Dockerfile"; IMAGE="slsteam-moon-builder"
fi

echo "==> portable build (using $runtime, image $IMAGE, ARCH=$ARCH)"

# Build (or reuse) the builder image. Tagging by name only, the layer cache
# inside the runtime keeps rebuilds cheap.
"$runtime" build -f "$DOCKERFILE" -t "$IMAGE" . >&2

# Run the build inside the container with the repo bind-mounted, so the
# resulting bin/ lands on the host without needing a docker cp.
# Docker runs as root: without --user the bind-mounted obj/ and bin/ would come back
# root-owned and the next host `make clean` would fail.  Rootless podman needs nothing.
user_args=()
[ "$runtime" = docker ] && user_args=(--user "$(id -u):$(id -g)")

"$runtime" run --rm "${user_args[@]}" \
	-v "$PWD:/build:Z" \
	-w /build \
	"$IMAGE" \
	bash -c "make clean && make ARCH=$ARCH"

scripts/check-pattern-refresh-abi.sh bin/pattern-refresh

echo "==> built: bin/SLSsteam.so, bin/library-inject.so, bin/pattern-refresh"
