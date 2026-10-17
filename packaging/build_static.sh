#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 MRG0721

#
# Builds and tests the static musl binaries.
#
#   ./packaging/build_static.sh [--rebuild-toolchain]
#
# The compiler lives in a pinned Alpine rootfs under build/musl/rootfs and is
# entered with bubblewrap, so nothing is installed on the host. The first run
# (or --rebuild-toolchain) downloads the rootfs and the toolchain packages and
# therefore needs network access. Later runs use the cache and work offline.
#
# Output: build/musl/build/cli/mxplorer (static-pie) plus the static test
# binaries. ctest runs core_operations, core_utils and cli_smoke against those
# binaries before the script returns.

set -euo pipefail

# Keep installed files independent of the caller's umask.
umask 022

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$project_root"

alpine_version="3.24.0"
alpine_arch="x86_64"
alpine_sha256="de9a11c0e0e7e9c94db3ed8af7b450eafc0b13687bd7e9199d55050f20aa0a89"
alpine_url="https://dl-cdn.alpinelinux.org/alpine/v3.24/releases/${alpine_arch}/alpine-minirootfs-${alpine_version}-${alpine_arch}.tar.gz"

musl_root="$project_root/build/musl"
rootfs="$musl_root/rootfs"
rootfs_stamp="$musl_root/rootfs-version"
rootfs_tarball="$musl_root/alpine-minirootfs-${alpine_version}-${alpine_arch}.tar.gz"
build_dir="$musl_root/build"

die()
{
    echo "error: $*" >&2
    exit 1
}

rebuild_toolchain=0
case "${1:-}" in
"") ;;
--rebuild-toolchain) rebuild_toolchain=1 ;;
*) die "unknown option '${1}' (usage: $0 [--rebuild-toolchain])" ;;
esac

for tool in bwrap curl tar sha256sum ctest file readelf objdump; do
    command -v "$tool" >/dev/null 2>&1 ||
        die "$tool is required to build and verify the static binaries"
done

toolchain_ready()
{
    [[ -f "$rootfs_stamp" && "$(cat "$rootfs_stamp")" == "$alpine_version" ]] &&
        [[ -x "$rootfs/usr/bin/g++" && -x "$rootfs/usr/bin/cmake" && -x "$rootfs/usr/bin/ninja" ]]
}

bootstrap_rootfs()
{
    mkdir -p "$musl_root"

    if [[ ! -f "$rootfs_tarball" ]]; then
        echo "==> download Alpine ${alpine_version} minirootfs (network needed on first run)"
        curl -fL --retry 3 -o "$rootfs_tarball.part" "$alpine_url" ||
            die "cannot download $alpine_url"
        mv "$rootfs_tarball.part" "$rootfs_tarball"
    fi

    echo "==> verify the pinned minirootfs checksum"
    printf '%s  %s\n' "$alpine_sha256" "$rootfs_tarball" | sha256sum -c - >/dev/null ||
        die "checksum mismatch for $rootfs_tarball"

    echo "==> unpack the toolchain rootfs"
    rm -rf "$rootfs"
    mkdir -p "$rootfs"
    tar -xzf "$rootfs_tarball" -C "$rootfs"

    echo "==> install the build packages (network needed on first run)"
    bwrap --unshare-user --uid 0 --gid 0 \
        --bind "$rootfs" / \
        --dev /dev --proc /proc \
        --ro-bind-try /etc/resolv.conf /etc/resolv.conf \
        -- /sbin/apk add --no-cache build-base gcc g++ musl-dev cmake ninja linux-headers ||
        die "cannot install the Alpine build packages"

    printf '%s\n' "$alpine_version" > "$rootfs_stamp"
}

if [[ $rebuild_toolchain -eq 1 ]] || ! toolchain_ready; then
    bootstrap_rootfs
fi

echo "==> configure and build (static, musl)"
rm -rf "$build_dir"
mkdir -p "$build_dir"

bwrap --unshare-user --uid 0 --gid 0 \
    --bind "$rootfs" / \
    --dev /dev --proc /proc \
    --bind "$project_root" "$project_root" \
    --bind "$build_dir" "$build_dir" \
    --setenv src "$project_root" \
    --setenv build "$build_dir" \
    -- /bin/sh -c \
    'cd "$build" && cmake -S "$src" -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_EXE_LINKER_FLAGS=-static \
        -DBUILD_TESTING=ON && ninja' ||
    die "the static build failed"

binary="$build_dir/cli/mxplorer"
[[ -x "$binary" ]] || die "no binary was built at $binary"

echo "==> verify that the binary is fully static"
file "$binary" | grep -Eq 'static-pie linked|statically linked' ||
    die "$binary is not statically linked"
if readelf -d "$binary" | grep -q NEEDED; then
    die "$binary still depends on shared libraries"
fi
if objdump -T "$binary" | grep -Eq 'GLIBC|GLIBCXX|CXXABI'; then
    die "$binary references versioned libc/libstdc++ symbols"
fi
ldd "$binary" 2>&1 | grep -Eq 'statically linked|not a dynamic executable' ||
    die "ldd does not consider $binary static"

echo "==> run the test suites against the static binaries"
ctest --test-dir "$build_dir" --output-on-failure

echo "==> record the toolchain"
bwrap --unshare-user --uid 0 --gid 0 \
    --bind "$rootfs" / \
    --dev /dev --proc /proc \
    -- /bin/sh -c 'echo "alpine: $(cat /etc/alpine-release)"; g++ --version | head -1' |
    tee "$musl_root/toolchain-info.txt"

echo
echo "binary  : $binary"
echo "version : $("$binary" --version)"
echo "size    : $(du -h "$binary" | cut -f1) (unstripped)"
