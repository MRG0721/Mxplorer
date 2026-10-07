#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 MRG0721

#
# End-to-end smoke test for the terminal front-end.
#
#   ./tests/smoke_test.sh ./build/cli/mxplorer
#
# Everything runs inside a throwaway directory under /tmp.

set -euo pipefail

MXPLORER="${1:-}"
if [[ -z "$MXPLORER" || ! -x "$MXPLORER" ]]; then
    echo "usage: $(basename "$0") /path/to/mxplorer" >&2
    exit 2
fi
MXPLORER="$(cd "$(dirname "$MXPLORER")" && pwd)/$(basename "$MXPLORER")"

WORK="$(mktemp -d)"
# Some checks deliberately leave directories without permissions behind, so
# give the owner its bits back before the final removal.
trap 'chmod -R u+rwX "$WORK" 2>/dev/null || true; rm -rf "$WORK"' EXIT

checks=0
fail() {
    echo "FAIL: $*" >&2
    exit 1
}
ok() {
    checks=$((checks + 1))
}
expect_ok() {
    if ! "$@" >/dev/null 2>&1; then
        fail "expected success: $*"
    fi
    ok
}
expect_fail() {
    if "$@" >/dev/null 2>&1; then
        fail "expected failure: $*"
    fi
    ok
}
contains() {
    if [[ "$1" != *"$2"* ]]; then
        fail "expected output to contain '$2', got: $1"
    fi
    ok
}
lines_of() {
    if [[ -z "$1" ]]; then
        echo 0
    else
        printf '%s\n' "$1" | wc -l
    fi
}

# ---------------------------------------------------------------- fixture ----
mkdir -p "$WORK/alpha"
echo "hello" >"$WORK/alpha/one.txt"
echo "two" >"$WORK/file2.txt"
echo "ten" >"$WORK/file10.txt"
ln -s file2.txt "$WORK/link"
ln -s nowhere "$WORK/dangling"
mkdir -p "$WORK/.hidden"
cd "$WORK"

# ---------------------------------------------------------------- listing ----
out="$("$MXPLORER" ls .)"
contains "$out" "alpha/"
contains "$out" "link@"
if [[ "$out" == *".hidden"* ]]; then
    fail "ls should hide dotfiles by default"
fi
ok

out="$("$MXPLORER" ls -a .)"
contains "$out" ".hidden/"

# Natural ordering: file2.txt must come before file10.txt.
out="$("$MXPLORER" ls -l .)"
contains "$out" "-rw"
first="$(grep -n 'file2\.txt' <<<"$out" | cut -d: -f1)"
second="$(grep -n 'file10\.txt' <<<"$out" | cut -d: -f1)"
if [[ -z "$first" || -z "$second" || "$first" -ge "$second" ]]; then
    fail "expected file2.txt before file10.txt in: $out"
fi
ok

# A dangling symlink has no readable timestamp and must not print garbage.
out="$("$MXPLORER" ls -l .)"
contains "$out" " - dangling"
if [[ "$out" == *2174* ]]; then
    fail "a dangling symlink must not print a bogus timestamp"
fi
ok

# Piped output must not contain ANSI escapes.
if [[ "$out" == *$'\033['* ]]; then
    fail "piped output should not contain ANSI escapes"
fi
ok

# ------------------------------------------------------------------- copy ----
expect_ok "$MXPLORER" cp file2.txt copy.txt
cmp -s file2.txt copy.txt || fail "cp produced different content"
ok

expect_ok "$MXPLORER" cp file2.txt alpha
[[ -f alpha/file2.txt ]] || fail "cp into a directory should place the file inside"
ok

expect_ok "$MXPLORER" cp -r alpha beta
[[ -f beta/one.txt ]] || fail "cp -r should copy the tree"
ok

expect_ok "$MXPLORER" cp link link2
[[ -L link2 ]] || fail "cp should recreate symlinks instead of following them"
ok

expect_fail "$MXPLORER" cp file2.txt file2.txt
expect_fail "$MXPLORER" cp -r alpha alpha/inner
expect_fail "$MXPLORER" cp alpha gamma
expect_ok "$MXPLORER" cp -f file2.txt copy.txt

# ------------------------------------------------------------------- move ----
expect_ok "$MXPLORER" mv file10.txt renamed.txt
[[ -f renamed.txt && ! -e file10.txt ]] || fail "mv should rename"
ok

expect_ok "$MXPLORER" mv renamed.txt beta
[[ -f beta/renamed.txt ]] || fail "mv into a directory should place the file inside"
ok

expect_fail "$MXPLORER" mv beta beta

# ---------------------------------------------------------------- mkdir/rm ----
expect_ok "$MXPLORER" mkdir -p deep/nested/dir
[[ -d deep/nested/dir ]] || fail "mkdir -p should create parents"
ok

out="$("$MXPLORER" mkdir -p deep)"
[[ -z "$out" ]] || fail "mkdir -p on an existing directory should be silent"
ok

expect_fail "$MXPLORER" mkdir deep
expect_fail "$MXPLORER" rm beta
expect_fail "$MXPLORER" rm missing.txt

out="$("$MXPLORER" rm -f missing.txt)"
[[ -z "$out" ]] || fail "rm -f on a missing path should be silent"
ok

expect_ok "$MXPLORER" rm -r deep
[[ ! -d deep ]] || fail "rm -r should remove the tree"
ok

expect_ok "$MXPLORER" rm -r beta
expect_ok "$MXPLORER" rm copy.txt link2

# --------------------------------------------------------- copy fidelity ----
# -p keeps permissions (setuid included) and timestamps.
mkdir -p "$WORK/fidelity"
printf 'x' >"$WORK/fidelity/mode.txt"
chmod 4755 "$WORK/fidelity/mode.txt"
touch -d '2001-02-03 04:05:06' "$WORK/fidelity/mode.txt"
expect_ok "$MXPLORER" cp -p "$WORK/fidelity/mode.txt" "$WORK/fidelity/mode-copy.txt"
[[ "$(stat -c '%a' "$WORK/fidelity/mode-copy.txt")" == "4755" ]] ||
    fail "cp -p must keep the setuid bit and the permission bits"
ok
source_mtime="$(stat -c '%Y' "$WORK/fidelity/mode.txt")"
copy_mtime="$(stat -c '%Y' "$WORK/fidelity/mode-copy.txt")"
[[ "$copy_mtime" == "$source_mtime" ]] || fail "cp -p must keep the modification time"
ok

# -p also reaches directories, not just regular files.
mkdir -p "$WORK/fidelity/dirmode/sub"
echo x >"$WORK/fidelity/dirmode/sub/file.txt"
chmod 750 "$WORK/fidelity/dirmode"
expect_ok "$MXPLORER" cp -r -p "$WORK/fidelity/dirmode" "$WORK/fidelity/dirmode-copy"
[[ "$(stat -c '%a' "$WORK/fidelity/dirmode-copy")" == "750" ]] ||
    fail "cp -r -p must keep directory permissions"
ok

# -p recreates hard links inside the copied tree; a plain copy does not.
mkdir -p "$WORK/fidelity/links"
echo "data" >"$WORK/fidelity/links/one"
ln "$WORK/fidelity/links/one" "$WORK/fidelity/links/two"
expect_ok "$MXPLORER" cp -r -p "$WORK/fidelity/links" "$WORK/fidelity/links-copy"
[[ "$WORK/fidelity/links-copy/one" -ef "$WORK/fidelity/links-copy/two" ]] ||
    fail "cp -r -p must recreate hard links"
ok
expect_ok "$MXPLORER" cp -r "$WORK/fidelity/links" "$WORK/fidelity/links-plain"
if [[ "$WORK/fidelity/links-plain/one" -ef "$WORK/fidelity/links-plain/two" ]]; then
    fail "a plain cp must keep the two files independent"
fi
ok

# -p carries extended attributes (ACLs are stored the same way).
if command -v python3 >/dev/null 2>&1 &&
    python3 -c 'import os,sys; open(sys.argv[1],"w").write("x"); os.setxattr(sys.argv[1],b"user.smoke",b"payload")' \
        "$WORK/fidelity/xattr" 2>/dev/null; then
    expect_ok "$MXPLORER" cp -p "$WORK/fidelity/xattr" "$WORK/fidelity/xattr-copy"
    if ! python3 -c 'import os,sys; sys.exit(0 if os.getxattr(sys.argv[1],b"user.smoke")==b"payload" else 1)' \
        "$WORK/fidelity/xattr-copy"; then
        fail "cp -p must preserve extended attributes"
    fi
    ok
else
    echo "note: user xattrs unavailable here, skipped the xattr check" >&2
fi

# Sparseness survives a copy: a 16 MiB file with one small extent must not
# turn into 16 MiB of written blocks.
truncate -s 16M "$WORK/fidelity/sparse"
printf 'data' | dd of="$WORK/fidelity/sparse" bs=1 seek=15000000 conv=notrunc status=none
expect_ok "$MXPLORER" cp "$WORK/fidelity/sparse" "$WORK/fidelity/sparse-copy"
cmp -s "$WORK/fidelity/sparse" "$WORK/fidelity/sparse-copy" ||
    fail "the sparse copy has different contents"
ok
sparse_blocks="$(stat -c '%b' "$WORK/fidelity/sparse-copy")"
((sparse_blocks * 512 < 8 * 1024 * 1024)) ||
    fail "the copy lost sparseness (allocated $((sparse_blocks * 512)) bytes)"
ok

# An interrupted copy must never leave a partial destination behind, and must
# clean up its temporary file.
mkdir -p "$WORK/fidelity/interrupt" "$WORK/fidelity/interrupt-dst"
dd if=/dev/zero of="$WORK/fidelity/interrupt/payload" bs=1M count=384 status=none
"$MXPLORER" cp "$WORK/fidelity/interrupt/payload" "$WORK/fidelity/interrupt-dst/payload" &
copy_pid=$!
sleep 0.05
kill -INT "$copy_pid" 2>/dev/null || true
copy_status=0
wait "$copy_pid" || copy_status=$?
leftovers="$(find "$WORK/fidelity/interrupt-dst" -name '*.mxplorer-partial.*' -print)"
[[ -z "$leftovers" ]] || fail "an interrupted copy left temporary files: $leftovers"
ok
interrupted_dest="$WORK/fidelity/interrupt-dst/payload"
if [[ -e "$interrupted_dest" ]]; then
    cmp -s "$WORK/fidelity/interrupt/payload" "$interrupted_dest" ||
        fail "the interrupted copy left a partial destination file"
    [[ "$copy_status" == "0" ]] ||
        fail "the copy reported failure but produced a destination file"
    ok
else
    [[ "$copy_status" != "0" ]] ||
        fail "the copy reported success without producing a destination file"
    ok
fi

# ------------------------------------------------------------ inspection ----
# A directory must not be copied or moved into itself through a symlinked
# spelling; the lexical check alone misses "cp -r src link/inner".
mkdir -p "$WORK/selfsrc"
echo "keep" >"$WORK/selfsrc/f.txt"
ln -s selfsrc "$WORK/selflink"
expect_fail "$MXPLORER" cp -r "$WORK/selfsrc" "$WORK/selflink/inner"
[[ "$(find "$WORK/selfsrc" -mindepth 1 | wc -l)" == "1" ]] ||
    fail "a refused self-copy polluted the source tree"
ok
expect_fail "$MXPLORER" mv "$WORK/selfsrc" "$WORK/selflink/inner"

# rm on a symlink removes the link, never what it points at.
mkdir -p "$WORK/target-dir"
echo "data" >"$WORK/target-dir/file"
ln -s target-dir "$WORK/dirlink"
expect_ok "$MXPLORER" rm "$WORK/dirlink"
[[ ! -e "$WORK/dirlink" && -f "$WORK/target-dir/file" ]] ||
    fail "rm must not follow a symlink"
ok

# Non-ASCII names sort by unsigned bytes: dot first, ASCII, then multi-byte.
mkdir -p "$WORK/sortcheck"
touch "$WORK/sortcheck/.hidden" "$WORK/sortcheck/z.txt" "$WORK/sortcheck/Ärger.txt"
out="$("$MXPLORER" ls -al "$WORK/sortcheck")"
first="$(grep -n '\.hidden' <<<"$out" | cut -d: -f1)"
second="$(grep -n 'z\.txt' <<<"$out" | cut -d: -f1)"
third="$(grep -n 'Ärger' <<<"$out" | cut -d: -f1)"
if [[ -z "$first" || -z "$second" || -z "$third" || "$first" -ge "$second" ||
    "$second" -ge "$third" ]]; then
    fail "non-ASCII names must sort by unsigned bytes: $out"
fi
ok

out="$("$MXPLORER" tree .)"
contains "$out" "directories,"
contains "$out" "one.txt"

out="$("$MXPLORER" tree file2.txt)"
contains "$out" "0 directories, 1 file"

out="$("$MXPLORER" stat file2.txt)"
contains "$out" "file"
contains "$out" "permissions"

out="$("$MXPLORER" stat dangling)"
contains "$out" "modified    -"

out="$("$MXPLORER" stat alpha)"
contains "$out" "size        -"

out="$("$MXPLORER" help)"
contains "$out" "mkdir"

out="$("$MXPLORER" help mv)"
contains "$out" "[-p]"

# ------------------------------------------------------------------- find ----
mkdir -p "$WORK/search/notes/deep"
echo "report" >"$WORK/search/report.txt"
echo "mixed" >"$WORK/search/MiXeD.TXT"
echo "report 2026" >"$WORK/search/notes/report-2026.txt"
echo "other" >"$WORK/search/notes/other.md"
echo "final" >"$WORK/search/notes/deep/final-report.txt"
echo "hidden" >"$WORK/search/.hidden-report.txt"
ln -s notes "$WORK/search/dirlink"

out="$("$MXPLORER" find '*.txt' "$WORK/search" 2>/dev/null)"
[[ "$(lines_of "$out")" == "3" ]] || fail "glob search returned: $out"
contains "$out" "report-2026.txt"

# A glob without wildcards means "contains this text".
out="$("$MXPLORER" find report "$WORK/search" 2>/dev/null)"
[[ "$(lines_of "$out")" == "3" ]] || fail "implicit substring search returned: $out"
ok

out="$("$MXPLORER" find -F '*.txt' "$WORK/search" 2>/dev/null)"
[[ -z "$out" ]] || fail "-F must treat the pattern literally, got: $out"
ok

out="$("$MXPLORER" find -E '^report.*\.txt$' "$WORK/search" 2>/dev/null)"
[[ "$(lines_of "$out")" == "2" ]] || fail "regex search returned: $out"
ok

out="$("$MXPLORER" find -i 'mixed.txt' "$WORK/search" 2>/dev/null)"
contains "$out" "MiXeD.TXT"

out="$("$MXPLORER" find -a '*.txt' "$WORK/search" 2>/dev/null)"
[[ "$(lines_of "$out")" == "4" ]] || fail "-a should include the hidden match: $out"
ok

out="$("$MXPLORER" find '*.txt' -d0 "$WORK/search" 2>/dev/null)"
[[ -z "$out" ]] || fail "-d 0 must match nothing, got: $out"
ok

out="$("$MXPLORER" find '*.txt' -d1 "$WORK/search" 2>/dev/null)"
[[ "$(lines_of "$out")" == "1" ]] || fail "-d 1 must not descend: $out"
ok

out="$("$MXPLORER" find '*.txt' -d2 "$WORK/search" 2>/dev/null)"
[[ "$(lines_of "$out")" == "2" ]] || fail "-d 2 should reach one level deeper: $out"
ok

out="$("$MXPLORER" find '*' -t d "$WORK/search" 2>/dev/null)"
[[ "$(lines_of "$out")" == "2" ]] || fail "-t d should list the two directories: $out"
ok

# Also proves that symbolic links are not followed.
out="$("$MXPLORER" find '*' -t l "$WORK/search" 2>/dev/null)"
contains "$out" "dirlink"

out="$("$MXPLORER" find '*.txt' -n1 "$WORK/search" 2>/dev/null)"
[[ "$(lines_of "$out")" == "1" ]] || fail "-n 1 should stop after one match: $out"
ok
err="$("$MXPLORER" find '*.txt' -n1 "$WORK/search" 2>&1 >/dev/null)"
contains "$err" "stopped at the -n limit"

out="$("$MXPLORER" find nothing-matches-this "$WORK/search" 2>/dev/null)"
[[ -z "$out" ]] || fail "a search without matches must be silent on stdout"
ok

out="$("$MXPLORER" search '*.md' "$WORK/search" 2>/dev/null)"
contains "$out" "other.md"

out="$("$MXPLORER" help find)"
contains "$out" "-n <count>"

expect_fail "$MXPLORER" find -E 'a(' "$WORK/search"
expect_fail "$MXPLORER" find 'x' "$WORK/no-such-directory"
expect_fail "$MXPLORER" find 'x' "$WORK/file2.txt"
expect_fail "$MXPLORER" find
expect_fail "$MXPLORER" find 'x' -Z
expect_fail "$MXPLORER" find 'x' -t q .

# ------------------------------------------------------- finder extras ----
mkdir -p "$WORK/content/notes"
echo "alpha needle" >"$WORK/content/note.txt"
echo "nothing here" >"$WORK/content/plain.txt"
printf 'zzz\000needle' >"$WORK/content/binary.dat"
echo "report" >"$WORK/content/notes/deep-report.txt"
head -c 4096 /dev/zero >"$WORK/content/big.dat"
touch -d '10 days ago' "$WORK/content/old.txt"

# Content search, binary safe, case folding honours -i.
out="$("$MXPLORER" find '*' "$WORK/content" -c needle 2>/dev/null)"
contains "$out" "note.txt"
contains "$out" "binary.dat"
out="$("$MXPLORER" find '*' "$WORK/content" -c NEEDLE 2>/dev/null)"
[[ -z "$out" ]] || fail "-c without -i must be case sensitive: $out"
ok
out="$("$MXPLORER" find '*' "$WORK/content" -c NEEDLE -i 2>/dev/null)"
contains "$out" "note.txt"

# Size and modification-time filters.
out="$("$MXPLORER" find '*' "$WORK/content" -t f -s +1K 2>/dev/null)"
[[ "$(lines_of "$out")" == "1" ]] || fail "-s +1K should only match the big file: $out"
contains "$out" "big.dat"
out="$("$MXPLORER" find '*' "$WORK/content" -t f -s -1K 2>/dev/null)"
if [[ "$out" == *"big.dat"* ]]; then
    fail "-s -1K must not match the big file: $out"
fi
ok
out="$("$MXPLORER" find '*' "$WORK/content" -t f -m +7d 2>/dev/null)"
[[ "$(lines_of "$out")" == "1" ]] || fail "-m +7d should only match the old file: $out"
contains "$out" "old.txt"
out="$("$MXPLORER" find '*' "$WORK/content" -t f -m -7d 2>/dev/null)"
if [[ "$out" == *"old.txt"* ]]; then
    fail "-m -7d must not match the old file: $out"
fi
ok

# Several roots in one command; a bad root fails before printing anything.
out="$("$MXPLORER" find '*.txt' "$WORK/content" "$WORK/search" 2>/dev/null)"
contains "$out" "note.txt"
contains "$out" "report.txt"
expect_fail "$MXPLORER" find '*.txt' "$WORK/content" "$WORK/definitely-missing"
out="$("$MXPLORER" find '*.txt' "$WORK/content" "$WORK/definitely-missing" 2>/dev/null || true)"
[[ -z "$out" ]] || fail "a bad root must be rejected before any match is printed"
ok

# Non-ASCII case folding (needs a UTF-8 locale; the test sets one explicitly).
printf 'x' >"$WORK/content/Ärger.txt"
out="$(LC_ALL=C.UTF-8 "$MXPLORER" find -F 'ärger' -i "$WORK/content" 2>/dev/null)"
contains "$out" "Ärger.txt"

# -i must not mangle regex escape classes: \D stays "not a digit".
printf 'x' >"$WORK/content/nXyZ2.txt"
out="$("$MXPLORER" find -E 'n\D+2' -i "$WORK/content" 2>/dev/null)"
contains "$out" "nXyZ2.txt"

# The new value options reject malformed input.
expect_fail "$MXPLORER" find '*' "$WORK/content" -s 10
expect_fail "$MXPLORER" find '*' "$WORK/content" -m 7d
expect_fail "$MXPLORER" find '*' "$WORK/content" -s +10Q
expect_fail "$MXPLORER" find '*' "$WORK/content" -m +7x
# A depth that does not fit an int must be rejected, not silently truncated
# (4294967297 used to wrap to 1 and quietly mean "direct children only").
expect_fail "$MXPLORER" find '*' "$WORK/content" -d 4294967297
expect_fail "$MXPLORER" find '*' "$WORK/content" -n 9223372036854775808

# ------------------------------------------------------------- reporting ----
expect_fail "$MXPLORER" cd /definitely/not/here
expect_fail "$MXPLORER" ls -z .
expect_fail "$MXPLORER" definitely-not-a-command

# ~user expands through the passwd database; an unknown user stays literal.
if getent passwd root >/dev/null 2>&1; then
    root_home="$(getent passwd root | cut -d: -f6)"
    out="$("$MXPLORER" stat '~root' 2>&1 || true)"
    contains "$out" "$root_home"
fi
expect_fail "$MXPLORER" stat '~no-such-user-mxplorer'

# The unknown option message must not carry a trailing space.
out="$("$MXPLORER" ls -z . 2>&1 | head -1)" || true
[[ "$out" == "error: ls: unknown option -z" ]] || fail "unexpected message: [$out]"
ok

# Starting with a deleted working directory must not abort the process.
# Note: remove_path() refusing to delete "/" is deliberately not tested here,
# because a regression in that guard would destroy the machine running the test.
mkdir -p "$WORK/gone"
cwd_exit="$(cd "$WORK/gone" && rmdir "$WORK/gone" && "$MXPLORER" ls >/dev/null 2>&1; echo $?)"
if [[ "$cwd_exit" != "0" && "$cwd_exit" != "1" ]]; then
    fail "a deleted working directory aborted the process (exit $cwd_exit)"
fi
ok

# An unreadable directory must be reported, not silently passed off as empty.
# Skipped as root, where file permissions do not apply.
if [[ "$(id -u)" != "0" ]]; then
    mkdir -p "$WORK/locked/inner"
    echo "secret" >"$WORK/locked/inner/file.txt"
    chmod 000 "$WORK/locked"

    expect_fail "$MXPLORER" ls "$WORK/locked"
    out="$("$MXPLORER" ls "$WORK/locked" 2>&1 || true)"
    contains "$out" "Permission denied"

    out="$("$MXPLORER" tree "$WORK" 2>&1 || true)"
    contains "$out" "[unreadable: Permission denied]"

    # A recursive copy must fail instead of quietly copying a partial tree.
    expect_fail "$MXPLORER" cp -r "$WORK/locked" "$WORK/locked-copy"
    expect_fail "$MXPLORER" rm -r "$WORK/locked"

    # A search reports the unreadable directory and keeps going...
    out="$("$MXPLORER" find '*' "$WORK" 2>&1)"
    contains "$out" "Permission denied"
    contains "$out" "1 unreadable"

    # ...but an unreadable root is the answer, not something to skip.
    expect_fail "$MXPLORER" find '*' "$WORK/locked"

    # An entry whose attributes cannot be read is still listed, with unknown
    # metadata, and reported instead of vanishing silently.
    mkdir -p "$WORK/attrs/inner"
    echo "secret" >"$WORK/attrs/inner/file.txt"
    chmod 400 "$WORK/attrs"
    out="$("$MXPLORER" ls "$WORK/attrs" 2>"$WORK/attrs.err")"
    contains "$out" "inner"
    contains "$(cat "$WORK/attrs.err")" "Permission denied"
    out="$("$MXPLORER" ls -l "$WORK/attrs" 2>/dev/null)"
    contains "$out" "?????????"
    out="$("$MXPLORER" find 'inner' "$WORK/attrs" 2>/dev/null)"
    contains "$out" "inner"
    err="$("$MXPLORER" find '*' "$WORK/attrs" 2>&1 >/dev/null)"
    contains "$err" "1 entry unreadable"
    chmod 755 "$WORK/attrs"

    chmod 755 "$WORK/locked"
    rm -rf "$WORK/locked-copy"
fi

# No temporary copy files may survive anywhere in the test tree.
leftovers="$(find "$WORK" -name '*.mxplorer-partial.*' -print)"
[[ -z "$leftovers" ]] || fail "temporary copy files were left behind: $leftovers"
ok

# --------------------------------------------------------- interactive ----
mkdir -p "my dir"
echo "spaced" >"my dir/spaced file.txt"

out="$(printf 'cd "my dir"\npwd\nls\nexit\n' | "$MXPLORER")"
contains "$out" "spaced file.txt"
contains "$out" "my dir\$"

# A trailing slash must not stick to the working directory (prompt and pwd).
out="$(printf 'cd "my dir"/\npwd\nexit\n' | "$MXPLORER")"
if [[ "$out" == *"my dir/"* ]]; then
    fail "a trailing slash leaked into the working directory: $out"
fi
ok

echo "all $checks checks passed"
