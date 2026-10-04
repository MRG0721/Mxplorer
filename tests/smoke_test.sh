#!/usr/bin/env bash
#
# End-to-end smoke test for the terminal front-end.
#
#   ./tests/smoke_test.sh ./build/cli/dentry
#
# Everything runs inside a throwaway directory under /tmp.

set -euo pipefail

DENTRY="${1:-}"
if [[ -z "$DENTRY" || ! -x "$DENTRY" ]]; then
    echo "usage: $(basename "$0") /path/to/dentry" >&2
    exit 2
fi
DENTRY="$(cd "$(dirname "$DENTRY")" && pwd)/$(basename "$DENTRY")"

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
out="$("$DENTRY" ls .)"
contains "$out" "alpha/"
contains "$out" "link@"
if [[ "$out" == *".hidden"* ]]; then
    fail "ls should hide dotfiles by default"
fi
ok

out="$("$DENTRY" ls -a .)"
contains "$out" ".hidden/"

# Natural ordering: file2.txt must come before file10.txt.
out="$("$DENTRY" ls -l .)"
contains "$out" "-rw"
first="$(grep -n 'file2\.txt' <<<"$out" | cut -d: -f1)"
second="$(grep -n 'file10\.txt' <<<"$out" | cut -d: -f1)"
if [[ -z "$first" || -z "$second" || "$first" -ge "$second" ]]; then
    fail "expected file2.txt before file10.txt in: $out"
fi
ok

# A dangling symlink has no readable timestamp and must not print garbage.
out="$("$DENTRY" ls -l .)"
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
expect_ok "$DENTRY" cp file2.txt copy.txt
cmp -s file2.txt copy.txt || fail "cp produced different content"
ok

expect_ok "$DENTRY" cp file2.txt alpha
[[ -f alpha/file2.txt ]] || fail "cp into a directory should place the file inside"
ok

expect_ok "$DENTRY" cp -r alpha beta
[[ -f beta/one.txt ]] || fail "cp -r should copy the tree"
ok

expect_ok "$DENTRY" cp link link2
[[ -L link2 ]] || fail "cp should recreate symlinks instead of following them"
ok

expect_fail "$DENTRY" cp file2.txt file2.txt
expect_fail "$DENTRY" cp -r alpha alpha/inner
expect_fail "$DENTRY" cp alpha gamma
expect_ok "$DENTRY" cp -f file2.txt copy.txt

# ------------------------------------------------------------------- move ----
expect_ok "$DENTRY" mv file10.txt renamed.txt
[[ -f renamed.txt && ! -e file10.txt ]] || fail "mv should rename"
ok

expect_ok "$DENTRY" mv renamed.txt beta
[[ -f beta/renamed.txt ]] || fail "mv into a directory should place the file inside"
ok

expect_fail "$DENTRY" mv beta beta

# ---------------------------------------------------------------- mkdir/rm ----
expect_ok "$DENTRY" mkdir -p deep/nested/dir
[[ -d deep/nested/dir ]] || fail "mkdir -p should create parents"
ok

out="$("$DENTRY" mkdir -p deep)"
[[ -z "$out" ]] || fail "mkdir -p on an existing directory should be silent"
ok

expect_fail "$DENTRY" mkdir deep
expect_fail "$DENTRY" rm beta
expect_fail "$DENTRY" rm missing.txt

out="$("$DENTRY" rm -f missing.txt)"
[[ -z "$out" ]] || fail "rm -f on a missing path should be silent"
ok

expect_ok "$DENTRY" rm -r deep
[[ ! -d deep ]] || fail "rm -r should remove the tree"
ok

expect_ok "$DENTRY" rm -r beta
expect_ok "$DENTRY" rm copy.txt link2

# ------------------------------------------------------------ inspection ----
out="$("$DENTRY" tree .)"
contains "$out" "directories,"
contains "$out" "one.txt"

out="$("$DENTRY" stat file2.txt)"
contains "$out" "file"
contains "$out" "permissions"

out="$("$DENTRY" stat dangling)"
contains "$out" "modified    -"

out="$("$DENTRY" stat alpha)"
contains "$out" "size        -"

out="$("$DENTRY" help)"
contains "$out" "mkdir"

out="$("$DENTRY" help mv)"
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

out="$("$DENTRY" find '*.txt' "$WORK/search" 2>/dev/null)"
[[ "$(lines_of "$out")" == "3" ]] || fail "glob search returned: $out"
contains "$out" "report-2026.txt"

# A glob without wildcards means "contains this text".
out="$("$DENTRY" find report "$WORK/search" 2>/dev/null)"
[[ "$(lines_of "$out")" == "3" ]] || fail "implicit substring search returned: $out"

out="$("$DENTRY" find -F '*.txt' "$WORK/search" 2>/dev/null)"
[[ -z "$out" ]] || fail "-F must treat the pattern literally, got: $out"
ok

out="$("$DENTRY" find -E '^report.*\.txt$' "$WORK/search" 2>/dev/null)"
[[ "$(lines_of "$out")" == "2" ]] || fail "regex search returned: $out"

out="$("$DENTRY" find -i 'mixed.txt' "$WORK/search" 2>/dev/null)"
contains "$out" "MiXeD.TXT"

out="$("$DENTRY" find -a '*.txt' "$WORK/search" 2>/dev/null)"
[[ "$(lines_of "$out")" == "4" ]] || fail "-a should include the hidden match: $out"

out="$("$DENTRY" find '*.txt' -d1 "$WORK/search" 2>/dev/null)"
[[ "$(lines_of "$out")" == "1" ]] || fail "-d 1 must not descend: $out"

out="$("$DENTRY" find '*' -t d "$WORK/search" 2>/dev/null)"
[[ "$(lines_of "$out")" == "2" ]] || fail "-t d should list the two directories: $out"

# Also proves that symbolic links are not followed.
out="$("$DENTRY" find '*' -t l "$WORK/search" 2>/dev/null)"
contains "$out" "dirlink"

out="$("$DENTRY" find '*.txt' -n1 "$WORK/search" 2>/dev/null)"
[[ "$(lines_of "$out")" == "1" ]] || fail "-n 1 should stop after one match: $out"
err="$("$DENTRY" find '*.txt' -n1 "$WORK/search" 2>&1 >/dev/null)"
contains "$err" "stopped at the -n limit"

out="$("$DENTRY" find nothing-matches-this "$WORK/search" 2>/dev/null)"
[[ -z "$out" ]] || fail "a search without matches must be silent on stdout"
ok

out="$("$DENTRY" search '*.md' "$WORK/search" 2>/dev/null)"
contains "$out" "other.md"

out="$("$DENTRY" help find)"
contains "$out" "-n <count>"

expect_fail "$DENTRY" find -E 'a(' "$WORK/search"
expect_fail "$DENTRY" find 'x' "$WORK/no-such-directory"
expect_fail "$DENTRY" find 'x' "$WORK/file2.txt"
expect_fail "$DENTRY" find
expect_fail "$DENTRY" find 'x' -Z
expect_fail "$DENTRY" find 'x' -t q .

# ------------------------------------------------------------- reporting ----
expect_fail "$DENTRY" cd /definitely/not/here
expect_fail "$DENTRY" ls -z .
expect_fail "$DENTRY" definitely-not-a-command

# The unknown option message must not carry a trailing space.
out="$("$DENTRY" ls -z . 2>&1 | head -1)" || true
[[ "$out" == "error: ls: unknown option -z" ]] || fail "unexpected message: [$out]"
ok

# Starting with a deleted working directory must not abort the process.
# Note: remove_path() refusing to delete "/" is deliberately not tested here,
# because a regression in that guard would destroy the machine running the test.
mkdir -p "$WORK/gone"
cwd_exit="$(cd "$WORK/gone" && rmdir "$WORK/gone" && "$DENTRY" ls >/dev/null 2>&1; echo $?)"
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

    expect_fail "$DENTRY" ls "$WORK/locked"
    out="$("$DENTRY" ls "$WORK/locked" 2>&1 || true)"
    contains "$out" "Permission denied"

    out="$("$DENTRY" tree "$WORK" 2>&1 || true)"
    contains "$out" "[unreadable: Permission denied]"

    # A recursive copy must fail instead of quietly copying a partial tree.
    expect_fail "$DENTRY" cp -r "$WORK/locked" "$WORK/locked-copy"
    expect_fail "$DENTRY" rm -r "$WORK/locked"

    # A search reports the unreadable directory and keeps going...
    out="$("$DENTRY" find '*' "$WORK" 2>&1)"
    contains "$out" "Permission denied"
    contains "$out" "1 unreadable"

    # ...but an unreadable root is the answer, not something to skip.
    expect_fail "$DENTRY" find '*' "$WORK/locked"

    chmod 755 "$WORK/locked"
    rm -rf "$WORK/locked-copy"
fi

# --------------------------------------------------------- interactive ----
mkdir -p "my dir"
echo "spaced" >"my dir/spaced file.txt"

out="$(printf 'cd "my dir"\npwd\nls\nexit\n' | "$DENTRY")"
contains "$out" "spaced file.txt"
contains "$out" "my dir\$"

echo "all $checks checks passed"
