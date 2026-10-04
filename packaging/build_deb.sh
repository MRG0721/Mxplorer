#!/usr/bin/env bash
#
# Builds a .deb for the mxplorer terminal front-end.
#
#   ./packaging/build_deb.sh
#
# Output: dist/mxplorer_<version>_<arch>.deb
#
# debhelper is not installed on this machine, so instead of running
# dpkg-buildpackage this script stages the install tree itself, writes the
# control files and lets dpkg-deb assemble the archive. The shared library
# dependencies are still computed by dpkg-shlibdeps, so the Depends field ends
# up the same as a debhelper build would produce.

set -euo pipefail

# Keep the staged modes independent of the caller's umask.
umask 022

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"

maintainer="mrg <mrg@localhost>"

die() {
    echo "error: $*" >&2
    exit 1
}

for tool in cmake dpkg-deb dpkg; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool is required to build the package"
done

# The project version in CMakeLists.txt is the single source of truth.
version="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\{1,\}\([0-9][^[:space:]]*\).*/\1/p' CMakeLists.txt | head -1)"
[[ -n "$version" ]] || die "cannot read the project version from CMakeLists.txt"

arch="$(dpkg --print-architecture)"
work="build/package"
stage="$work/mxplorer_${version}_${arch}"
dist="dist"

# Lets dpkg-shlibdeps do its job: it insists on finding debian/control and
# debian/changelog next to the working directory.
resolve_depends() {
    local binary_path="$1"

    if ! command -v dpkg-shlibdeps >/dev/null 2>&1; then
        printf 'libc6, libstdc++6'
        return 0
    fi

    local sandbox="$work/depends"
    rm -rf "$sandbox"
    mkdir -p "$sandbox/debian"

    sed -e "s|@MAINTAINER@|$maintainer|" \
        packaging/debian-control.in > "$sandbox/debian/control"
    sed -e "s|@VERSION@|$version|" \
        -e "s|@DATE@|$(LC_ALL=C date -R)|" \
        packaging/changelog > "$sandbox/debian/changelog"

    # dpkg-shlibdeps runs from inside the sandbox, so the binary has to be
    # named by absolute path.
    local resolved
    resolved="$(cd "$sandbox" && dpkg-shlibdeps -O -e"$binary_path" 2>/dev/null |
        sed -n 's/^shlibs:Depends=//p')" || return 0
    printf '%s' "$resolved"
}

rm -rf "$work"
mkdir -p "$dist"

echo "==> configure and build (Release)"
cmake -S . -B "$work/cmake" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr >/dev/null
cmake --build "$work/cmake" >/dev/null

echo "==> install into $stage"
DESTDIR="$stage" cmake --install "$work/cmake" >/dev/null

binary="$stage/usr/bin/mxplorer"
binary_path="$(realpath "$stage/usr/bin/mxplorer")"
[[ -x "$binary_path" ]] || die "no binary was installed to $binary"

# The program and the package must not disagree about their version.
binary_version="$("$binary_path" --version | awk '{print $2}')"
[[ "$binary_version" == "$version" ]] ||
    die "the binary reports $binary_version, the package would be $version"

# Substitute the real version and compress: Debian ships man pages gzipped.
sed -i "s|@VERSION@|$version|" "$stage/usr/share/man/man1/mxplorer.1"
gzip -9 -n "$stage/usr/share/man/man1/mxplorer.1"

echo "==> resolve shared library dependencies"
depends="$(resolve_depends "$binary_path")" || depends=""
[[ -n "$depends" ]] || depends="libc6, libstdc++6"

installed_size="$(du -sk "$stage/usr" | cut -f1)"

echo "==> write control files"
mkdir -p "$stage/DEBIAN" "$stage/usr/share/doc/mxplorer"

sed -e "s|@VERSION@|$version|" \
    -e "s|@ARCH@|$arch|" \
    -e "s|@DEPENDS@|$depends|" \
    -e "s|@INSTALLED_SIZE@|$installed_size|" \
    packaging/control.in > "$stage/DEBIAN/control"

install -m 0644 packaging/copyright "$stage/usr/share/doc/mxplorer/copyright"
sed -e "s|@VERSION@|$version|" \
    -e "s|@DATE@|$(LC_ALL=C date -R)|" \
    packaging/changelog |
    gzip -9 -n -c > "$stage/usr/share/doc/mxplorer/changelog.gz"

# md5sums for everything outside /usr/share/doc, as policy asks for.
(cd "$stage" &&
    find usr -type f ! -path 'usr/share/doc/*' -exec md5sum {} + |
    LC_ALL=C sort -k2) > "$stage/DEBIAN/md5sums"

echo "==> assemble the archive"
out="$dist/mxplorer_${version}_${arch}.deb"
rm -f "$out"
dpkg-deb --root-owner-group --build "$stage" "$out" >/dev/null

echo
echo "package : $out"
echo "size    : $(du -h "$out" | cut -f1)"
echo "depends : $depends"
echo "contents:"
dpkg-deb --contents "$out" | awk '{ printf "  %s\n", $6 }'
