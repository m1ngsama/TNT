#!/bin/sh
# Manual regression tests for scripts/check_maintainers.sh.

set -u

PASS=0
FAIL=0
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

pass() {
    echo "PASS $1"
    PASS=$((PASS + 1))
}

fail_case() {
    echo "FAIL $1"
    FAIL=$((FAIL + 1))
}

echo "=== TNT Maintainer Coverage Tests ==="

if "$ROOT/scripts/check_maintainers.sh" src/module_runtime.c docs/MODULE_PROTOCOL.md >/dev/null; then
    pass "known paths are covered"
else
    fail_case "known paths are covered"
fi

if "$ROOT/scripts/check_maintainers.sh" no/such/path >/dev/null 2>&1; then
    fail_case "unknown path is rejected"
else
    pass "unknown path is rejected"
fi

if output=$("$ROOT/scripts/check_maintainers.sh" 2>&1); then
    pass "repository file set is covered"
else
    printf '%s\n' "$output"
    fail_case "repository file set is covered"
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/tnt-maintainers.XXXXXX") || exit 1
cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT INT TERM

mkdir -p "$tmp/include" "$tmp/src" "$tmp/scripts" "$tmp/obj" "$tmp/tests/unit"
cp "$ROOT/MAINTAINERS" "$tmp/MAINTAINERS"
cp "$ROOT/scripts/check_maintainers.sh" "$tmp/scripts/check_maintainers.sh"
cp "$ROOT/scripts/get_maintainer.sh" "$tmp/scripts/get_maintainer.sh"
chmod +x "$tmp/scripts/check_maintainers.sh" "$tmp/scripts/get_maintainer.sh"
: > "$tmp/include/common.h"
: > "$tmp/src/main.c"
: > "$tmp/tests/unit/test_utf8.c"
: > "$tmp/obj/main.o"
: > "$tmp/tnt"
: > "$tmp/tntctl"
: > "$tmp/tests/unit/test_utf8"
: > "$tmp/tests/unit/test_utf8.o"
: > "$tmp/tests/unit/test_messages.log"
chmod +x "$tmp/tests/unit/test_utf8"

if (cd "$tmp" && scripts/check_maintainers.sh >/dev/null); then
    pass "source archive fallback ignores generated files"
else
    fail_case "source archive fallback ignores generated files"
fi

printf '\nPASSED: %d\nFAILED: %d\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
