#!/bin/sh
# Manual regression tests for scripts/get_maintainer.sh.

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

echo "=== TNT Maintainer Map Tests ==="

MODULE_OUTPUT=$("$ROOT/scripts/get_maintainer.sh" src/module_runtime.c)
if printf '%s\n' "$MODULE_OUTPUT" |
       awk -F'\t' '$2 == "MODULE CORE INTERFACE" && $4 == "src/module_*.c" { found = 1 } END { exit found ? 0 : 1 }'; then
    pass "module runtime maps to module core interface"
else
    fail_case "module runtime maps to module core interface"
    printf '%s\n' "$MODULE_OUTPUT"
fi

DOC_OUTPUT=$("$ROOT/scripts/get_maintainer.sh" docs/MODULE_PROTOCOL.md)
if printf '%s\n' "$DOC_OUTPUT" |
       awk -F'\t' '$2 == "MODULE CORE INTERFACE" { module = 1 } $2 == "DOCUMENTATION" { docs = 1 } END { exit module && docs ? 0 : 1 }'; then
    pass "module protocol maps to module and documentation areas"
else
    fail_case "module protocol maps to module and documentation areas"
    printf '%s\n' "$DOC_OUTPUT"
fi

UNKNOWN_OUTPUT=$("$ROOT/scripts/get_maintainer.sh" no/such/path)
if printf '%s\n' "$UNKNOWN_OUTPUT" |
       awk -F'\t' '$2 == "UNKNOWN" && $3 == "unknown" { found = 1 } END { exit found ? 0 : 1 }'; then
    pass "unknown paths report unknown"
else
    fail_case "unknown paths report unknown"
    printf '%s\n' "$UNKNOWN_OUTPUT"
fi

if "$ROOT/scripts/get_maintainer.sh" >/dev/null 2>&1; then
    fail_case "missing path exits nonzero"
else
    pass "missing path exits nonzero"
fi

printf '\nPASSED: %d\nFAILED: %d\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
