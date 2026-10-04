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
trap 'rm -rf "$WORK"' EXIT

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

# ------------------------------------------------------------ inspection ----
out="$("$MXPLORER" tree .)"
contains "$out" "directories,"
contains "$out" "one.txt"

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

# ------------------------------------------------------------- reporting ----
expect_fail "$MXPLORER" cd /definitely/not/here
expect_fail "$MXPLORER" ls -z .
expect_fail "$MXPLORER" definitely-not-a-command

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

    chmod 755 "$WORK/locked"
    rm -rf "$WORK/locked-copy"
fi

# --------------------------------------------------------- interactive ----
mkdir -p "my dir"
echo "spaced" >"my dir/spaced file.txt"

out="$(printf 'cd "my dir"\npwd\nls\nexit\n' | "$MXPLORER")"
contains "$out" "spaced file.txt"
contains "$out" "my dir\$"

echo "all $checks checks passed"
