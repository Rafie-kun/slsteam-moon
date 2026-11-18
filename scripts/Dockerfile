# Builder image for slsteam-moon's portable build.
#
# Ubuntu 22.04 supplies the release toolchain. Its glibc is 2.35, while the
# produced helper is policy-gated to require no symbols newer than GLIBC_2.34
# (scripts/check-sls-abi.sh runs on every package). Resulting binaries target
# Ubuntu 22.04+, Debian 12+, Fedora 36+, and modern Mint/Pop!_OS/Zorin
# releases.
#
# Used by scripts/build.sh --portable (which invokes
# scripts/_build-portable.sh). Source is bind-mounted at run time;
# this image only carries the toolchain.
FROM ubuntu:22.04

# amd64 toolchain and the dev headers the Makefile links (-m64 builds; the
# library must match Steam's 64-bit steamrt64 client).
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get \
        install -y --no-install-recommends \
        ca-certificates \
        g++ \
        libcurl4-openssl-dev:amd64 \
        libssl-dev:amd64 \
        make \
        pkg-config \
        zip && \
    rm -rf /var/lib/apt/lists/*

# pkg-config search path (matches steamrt64 / x86_64 builds).
ENV PKG_CONFIG_PATH=/usr/lib/x86_64-linux-gnu/pkgconfig

WORKDIR /build
