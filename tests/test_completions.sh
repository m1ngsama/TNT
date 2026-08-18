#!/usr/bin/env bash

set -eu

PASS=0
FAIL=0
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)

pass() {
    printf 'PASS %s\n' "$1"
    PASS=$((PASS + 1))
}

fail() {
    printf 'FAIL %s\n' "$1"
    FAIL=$((FAIL + 1))
}

complete_bash() {
    COMP_WORDS=("$@")
    COMP_CWORD=$((${#COMP_WORDS[@]} - 1))
    COMPREPLY=()
    _tntctl
    printf '%s\n' "${COMPREPLY[@]-}"
}

expect_word() {
    words=$1
    expected=$2
    label=$3
    if grep -Fx -q -- "$expected" <<<"$words"; then
        pass "$label"
    else
        fail "$label"
    fi
}

forbid_word() {
    words=$1
    forbidden=$2
    label=$3
    if grep -Fx -q -- "$forbidden" <<<"$words"; then
        fail "$label"
    else
        pass "$label"
    fi
}

source "$ROOT/packaging/completions/tntctl.bash"

global=$(complete_bash tntctl --)
expect_word "$global" --port "bash offers global options before the host"

stats=$(complete_bash tntctl host stats --)
expect_word "$stats" --json "bash offers stats JSON output"
forbid_word "$stats" --port "bash hides global options after a command"

tail=$(complete_bash tntctl host tail -)
expect_word "$tail" -n "bash offers tail record count"

dump=$(complete_bash tntctl host dump -)
expect_word "$dump" -n "bash offers dump record count"
expect_word "$dump" --all "bash offers complete dump"

post=$(complete_bash tntctl host post -)
if [[ -z "$post" ]]; then
    pass "bash offers no invalid post options"
else
    fail "bash offers no invalid post options"
fi

if command -v zsh >/dev/null 2>&1; then
    if zsh -n "$ROOT/packaging/completions/_tntctl"; then
        pass "zsh completion syntax"
    else
        fail "zsh completion syntax"
    fi
else
    echo "SKIP zsh completion syntax (zsh not installed)"
fi

if command -v fish >/dev/null 2>&1; then
    if fish -n "$ROOT/packaging/completions/tntctl.fish"; then
        pass "fish completion syntax"
    else
        fail "fish completion syntax"
    fi
else
    echo "SKIP fish completion syntax (fish not installed)"
fi

if grep -F -q 'dump) compadd -- -n --all' \
        "$ROOT/packaging/completions/_tntctl" &&
   grep -F -q -- "-s n -d 'Record count'" \
        "$ROOT/packaging/completions/tntctl.fish" &&
   grep -F -q -- "-l all -d 'All records'" \
        "$ROOT/packaging/completions/tntctl.fish"; then
    pass "zsh and fish expose tail and dump options"
else
    fail "zsh and fish expose tail and dump options"
fi

printf '\nPASSED: %d\nFAILED: %d\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
