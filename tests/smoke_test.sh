#!/usr/bin/env bash
#
# End-to-end smoke test for the terminal front-end.
#
#   ./tests/smoke_test.sh ./build/cli/fman
#
# Everything runs inside a throwaway directory under /tmp.

set -euo pipefail

FMAN="${1:-}"
if [[ -z "$FMAN" || ! -x "$FMAN" ]]; then
    echo "usage: $(basename "$0") /path/to/fman" >&2
    exit 2
fi
FMAN="$(cd "$(dirname "$FMAN")" && pwd)/$(basename "$FMAN")"

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
out="$("$FMAN" ls .)"
contains "$out" "alpha/"
contains "$out" "link@"
if [[ "$out" == *".hidden"* ]]; then
    fail "ls should hide dotfiles by default"
fi
ok

out="$("$FMAN" ls -a .)"
contains "$out" ".hidden/"

# Natural ordering: file2.txt must come before file10.txt.
out="$("$FMAN" ls -l .)"
contains "$out" "-rw"
first="$(grep -n 'file2\.txt' <<<"$out" | cut -d: -f1)"
second="$(grep -n 'file10\.txt' <<<"$out" | cut -d: -f1)"
if [[ -z "$first" || -z "$second" || "$first" -ge "$second" ]]; then
    fail "expected file2.txt before file10.txt in: $out"
fi
ok

# A dangling symlink has no readable timestamp and must not print garbage.
out="$("$FMAN" ls -l .)"
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
expect_ok "$FMAN" cp file2.txt copy.txt
cmp -s file2.txt copy.txt || fail "cp produced different content"
ok

expect_ok "$FMAN" cp file2.txt alpha
[[ -f alpha/file2.txt ]] || fail "cp into a directory should place the file inside"
ok

expect_ok "$FMAN" cp -r alpha beta
[[ -f beta/one.txt ]] || fail "cp -r should copy the tree"
ok

expect_ok "$FMAN" cp link link2
[[ -L link2 ]] || fail "cp should recreate symlinks instead of following them"
ok

expect_fail "$FMAN" cp file2.txt file2.txt
expect_fail "$FMAN" cp -r alpha alpha/inner
expect_fail "$FMAN" cp alpha gamma
expect_ok "$FMAN" cp -f file2.txt copy.txt

# ------------------------------------------------------------------- move ----
expect_ok "$FMAN" mv file10.txt renamed.txt
[[ -f renamed.txt && ! -e file10.txt ]] || fail "mv should rename"
ok

expect_ok "$FMAN" mv renamed.txt beta
[[ -f beta/renamed.txt ]] || fail "mv into a directory should place the file inside"
ok

expect_fail "$FMAN" mv beta beta

# ---------------------------------------------------------------- mkdir/rm ----
expect_ok "$FMAN" mkdir -p deep/nested/dir
[[ -d deep/nested/dir ]] || fail "mkdir -p should create parents"
ok

out="$("$FMAN" mkdir -p deep)"
[[ -z "$out" ]] || fail "mkdir -p on an existing directory should be silent"
ok

expect_fail "$FMAN" mkdir deep
expect_fail "$FMAN" rm beta
expect_fail "$FMAN" rm missing.txt

out="$("$FMAN" rm -f missing.txt)"
[[ -z "$out" ]] || fail "rm -f on a missing path should be silent"
ok

expect_ok "$FMAN" rm -r deep
[[ ! -d deep ]] || fail "rm -r should remove the tree"
ok

expect_ok "$FMAN" rm -r beta
expect_ok "$FMAN" rm copy.txt link2

# ------------------------------------------------------------ inspection ----
out="$("$FMAN" tree .)"
contains "$out" "directories,"
contains "$out" "one.txt"

out="$("$FMAN" stat file2.txt)"
contains "$out" "file"
contains "$out" "permissions"

out="$("$FMAN" stat dangling)"
contains "$out" "modified    -"

out="$("$FMAN" stat alpha)"
contains "$out" "size        -"

out="$("$FMAN" help)"
contains "$out" "mkdir"

out="$("$FMAN" help mv)"
contains "$out" "[-p]"

# ------------------------------------------------------------- reporting ----
expect_fail "$FMAN" cd /definitely/not/here
expect_fail "$FMAN" ls -z .
expect_fail "$FMAN" definitely-not-a-command

# The unknown option message must not carry a trailing space.
out="$("$FMAN" ls -z . 2>&1 | head -1)" || true
[[ "$out" == "error: ls: unknown option -z" ]] || fail "unexpected message: [$out]"
ok

# Starting with a deleted working directory must not abort the process.
# Note: remove_path() refusing to delete "/" is deliberately not tested here,
# because a regression in that guard would destroy the machine running the test.
mkdir -p "$WORK/gone"
cwd_exit="$(cd "$WORK/gone" && rmdir "$WORK/gone" && "$FMAN" ls >/dev/null 2>&1; echo $?)"
if [[ "$cwd_exit" != "0" && "$cwd_exit" != "1" ]]; then
    fail "a deleted working directory aborted the process (exit $cwd_exit)"
fi
ok

# --------------------------------------------------------- interactive ----
mkdir -p "my dir"
echo "spaced" >"my dir/spaced file.txt"

out="$(printf 'cd "my dir"\npwd\nls\nexit\n' | "$FMAN")"
contains "$out" "spaced file.txt"
contains "$out" "my dir\$"

echo "all $checks checks passed"
