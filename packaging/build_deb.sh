#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 MRG0721

#
# Builds the static release artifacts.
#
#   ./packaging/build_deb.sh
#
# Output:
#   dist/Mxplorer-<version>-<platform>-<arch>.deb
#   dist/Mxplorer-<version>-<platform>-linux-<arch>-static.tar.gz
#
# <platform> is a build-platform tag in the file name. It is "forky" until the
# compatibility matrix has been verified by hand; set platform="" below to
# drop it.
#
# The binary is produced by packaging/build_static.sh, which builds and tests a
# static-pie binary inside a pinned Alpine rootfs. The deb deliberately has no
# Depends field: the program carries musl and libstdc++ inside itself. The
# tarball is for systems that do not use dpkg.

set -euo pipefail

# Keep the staged modes independent of the caller's umask.
umask 022

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$project_root"

maintainer="MRG0721 <271227737+MRG0721@users.noreply.github.com>"

# The artifact carries the brand spelling; the package itself stays lowercase.
brand="Mxplorer"

# Build-platform tag in the artifact names. It names the environment that
# produced the package. Once compatibility has been verified by hand, set this
# to an empty string so the deb becomes Mxplorer-<version>-<arch>.deb.
platform="forky"

die()
{
    echo "error: $*" >&2
    exit 1
}

for tool in dpkg-deb dpkg strip gzip md5sum tar; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool is required to build the package"
done

# The feature version comes from project(VERSION); the release tag is the
# second half of the version string. Both live in CMakeLists.txt.
version_base="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\{1,\}\([0-9][^[:space:]]*\).*/\1/p' CMakeLists.txt |
    head -1)"
[[ -n "$version_base" ]] || die "cannot read the project version from CMakeLists.txt"

release_tag="$(sed -n 's/^[[:space:]]*set(MXPLORER_RELEASE_TAG[[:space:]]*"\([^"]*\)".*/\1/p' CMakeLists.txt |
    head -1)"
version="$version_base"
[[ -z "$release_tag" ]] || version="$version_base-$release_tag"

arch="$(dpkg --print-architecture)"
[[ "$arch" == "amd64" ]] ||
    die "the pinned static toolchain currently targets amd64 only"

build_dir="$project_root/build/musl/build"
stage="$project_root/build/musl/stage"
dist="$project_root/dist"

echo "==> build the static binary (this runs the test suites)"
"$project_root/packaging/build_static.sh"

binary="$build_dir/cli/mxplorer"
[[ -x "$binary" ]] || die "no binary at $binary"

echo "==> stage the install tree"
rm -rf "$stage"
mkdir -p "$stage/usr/bin" "$stage/usr/share/man/man1" "$stage/usr/share/doc/mxplorer" "$dist"

install -m 0755 "$binary" "$stage/usr/bin/mxplorer"
strip --strip-all "$stage/usr/bin/mxplorer"

# Debian ships man pages gzipped; substitute the package version first.
sed -e "s|@MXPLORER_VERSION_STRING@|$version|" packaging/mxplorer.1 |
    gzip -9n > "$stage/usr/share/man/man1/mxplorer.1.gz"

install -m 0644 packaging/copyright "$stage/usr/share/doc/mxplorer/copyright"
sed -e "s|@VERSION@|$version|" \
    -e "s|@DATE@|$(LC_ALL=C date -R)|" \
    packaging/changelog |
    gzip -9n > "$stage/usr/share/doc/mxplorer/changelog.gz"

# The program and the package must not disagree about their version.
binary_version="$("$stage/usr/bin/mxplorer" --version | awk '{print $2}')"
[[ "$binary_version" == "$version" ]] ||
    die "the binary reports $binary_version, the package would be $version"

installed_size="$(du -sk "$stage/usr" | cut -f1)"

echo "==> write control files"
mkdir -p "$stage/DEBIAN"

sed -e "s|@VERSION@|$version|" \
    -e "s|@ARCH@|$arch|" \
    -e "s|@INSTALLED_SIZE@|$installed_size|" \
    packaging/control.in > "$stage/DEBIAN/control"

# md5sums for everything outside /usr/share/doc, as policy asks for.
(cd "$stage" &&
    find usr -type f ! -path 'usr/share/doc/*' -exec md5sum {} + |
    LC_ALL=C sort -k2) > "$stage/DEBIAN/md5sums"

echo "==> assemble the archive"
out="$dist/${brand}-${version}${platform:+-$platform}-${arch}.deb"
rm -f "$out"
dpkg-deb --root-owner-group --build "$stage" "$out" >/dev/null

tar_out="$dist/${brand}-${version}${platform:+-$platform}-linux-${arch}-static.tar.gz"
rm -f "$tar_out"
tar --sort=name --mtime='UTC 1970-01-01' --owner=0 --group=0 --numeric-owner \
    -cf - -C "$stage" usr |
    gzip -9n > "$tar_out"

echo
echo "package : $out"
echo "size    : $(du -h "$out" | cut -f1)"
echo "depends : none (static-pie, no shared libraries)"
echo "tarball : $tar_out ($(du -h "$tar_out" | cut -f1))"
echo "contents:"
dpkg-deb --contents "$out" | awk '{ printf "  %s\n", $6 }'
