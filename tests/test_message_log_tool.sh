#!/bin/sh
# Offline messages.log check/recover regression tests.

set -u

PASS=0
FAIL=0
BIN="../tnt"
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-log-tool-test.XXXXXX")

cleanup() {
    rm -rf "$STATE_DIR"
}
trap cleanup EXIT

pass() {
    echo "✓ $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "✗ $1"
    FAIL=$((FAIL + 1))
}

ts_now() {
    date -u +%Y-%m-%dT%H:%M:%SZ
}

echo "=== TNT Message Log Tool Tests ==="

if [ ! -x "$BIN" ]; then
    echo "Error: binary $BIN not found. Run make first."
    exit 1
fi

TS=$(ts_now)
CLEAN_LOG="$STATE_DIR/clean.log"
cat > "$CLEAN_LOG" <<EOF
$TS|alice|one
$TS|bob|two
EOF

CHECK_OUTPUT=$("$BIN" --log-check "$CLEAN_LOG" 2>&1)
CHECK_STATUS=$?
printf '%s\n' "$CHECK_OUTPUT" | grep -q '^valid_records 2$' &&
printf '%s\n' "$CHECK_OUTPUT" | grep -q '^invalid_records 0$'
if [ "$CHECK_STATUS" -eq 0 ] && [ $? -eq 0 ]; then
    pass "clean log check exits 0"
else
    fail "clean log check"
    printf '%s\n' "$CHECK_OUTPUT"
    echo "exit status: $CHECK_STATUS"
fi

BAD_LOG="$STATE_DIR/bad.log"
cat > "$BAD_LOG" <<EOF
$TS|alice|one
$TS|mallory|extra|pipe
$TS|bob|two
EOF
printf '%s|partial|unterminated' "$TS" >> "$BAD_LOG"

BAD_CHECK_OUTPUT=$("$BIN" --log-check "$BAD_LOG" 2>&1)
BAD_CHECK_STATUS=$?
printf '%s\n' "$BAD_CHECK_OUTPUT" | grep -q '^records_seen 4$' &&
printf '%s\n' "$BAD_CHECK_OUTPUT" | grep -q '^valid_records 2$' &&
printf '%s\n' "$BAD_CHECK_OUTPUT" | grep -q '^invalid_records 2$' &&
printf '%s\n' "$BAD_CHECK_OUTPUT" | grep -q '^first_invalid_line 2$'
if [ "$BAD_CHECK_STATUS" -eq 1 ] && [ $? -eq 0 ]; then
    pass "bad log check reports skipped records"
else
    fail "bad log check"
    printf '%s\n' "$BAD_CHECK_OUTPUT"
    echo "exit status: $BAD_CHECK_STATUS"
fi

RECOVERED="$STATE_DIR/recovered.log"
RECOVER_REPORT="$STATE_DIR/recover.report"
"$BIN" --log-recover "$BAD_LOG" > "$RECOVERED" 2> "$RECOVER_REPORT"
RECOVER_STATUS=$?
if [ "$RECOVER_STATUS" -eq 1 ] &&
   grep -q '^valid_records 2$' "$RECOVER_REPORT" &&
   grep -q '^invalid_records 2$' "$RECOVER_REPORT" &&
   grep -q "$TS|alice|one" "$RECOVERED" &&
   grep -q "$TS|bob|two" "$RECOVERED" &&
   ! grep -q 'mallory' "$RECOVERED" &&
   ! grep -q 'partial' "$RECOVERED"; then
    pass "recover writes valid records and reports skipped records"
else
    fail "bad log recovery"
    cat "$RECOVERED" 2>/dev/null
    cat "$RECOVER_REPORT" 2>/dev/null
    echo "exit status: $RECOVER_STATUS"
fi

MISSING_OUTPUT=$("$BIN" --log-check "$STATE_DIR/missing.log" 2>&1)
MISSING_STATUS=$?
if [ "$MISSING_STATUS" -eq 1 ] &&
   printf '%s\n' "$MISSING_OUTPUT" | grep -q 'No such file'; then
    pass "missing log exits 1"
else
    fail "missing log handling"
    printf '%s\n' "$MISSING_OUTPUT"
    echo "exit status: $MISSING_STATUS"
fi

USAGE_OUTPUT=$("$BIN" --log-check 2>&1)
USAGE_STATUS=$?
if [ "$USAGE_STATUS" -eq 64 ] &&
   printf '%s\n' "$USAGE_OUTPUT" | grep -q 'Option requires argument: --log-check'; then
    pass "missing log-check argument exits 64"
else
    fail "missing log-check argument"
    printf '%s\n' "$USAGE_OUTPUT"
    echo "exit status: $USAGE_STATUS"
fi

CONFLICT_OUTPUT=$("$BIN" --log-check "$CLEAN_LOG" --log-recover "$CLEAN_LOG" 2>&1)
CONFLICT_STATUS=$?
if [ "$CONFLICT_STATUS" -eq 64 ] &&
   printf '%s\n' "$CONFLICT_OUTPUT" | grep -q 'Invalid --log-check: --log-recover'; then
    pass "conflicting log modes exit 64"
else
    fail "conflicting log modes"
    printf '%s\n' "$CONFLICT_OUTPUT"
    echo "exit status: $CONFLICT_STATUS"
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
