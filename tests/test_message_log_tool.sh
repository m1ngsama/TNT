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
if [ "$CHECK_STATUS" -eq 0 ] &&
   printf '%s\n' "$CHECK_OUTPUT" | grep -q '^valid_records 2$' &&
   printf '%s\n' "$CHECK_OUTPUT" | grep -q '^invalid_records 0$'; then
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
if [ "$BAD_CHECK_STATUS" -eq 1 ] &&
   printf '%s\n' "$BAD_CHECK_OUTPUT" | grep -q '^records_seen 4$' &&
   printf '%s\n' "$BAD_CHECK_OUTPUT" | grep -q '^valid_records 2$' &&
   printf '%s\n' "$BAD_CHECK_OUTPUT" | grep -q '^invalid_records 2$' &&
   printf '%s\n' "$BAD_CHECK_OUTPUT" | grep -q '^first_invalid_line 2$'; then
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

V2_LOG="$STATE_DIR/v2.log"
printf '#tnt-message-log v2\n' > "$V2_LOG"
printf '%s|alice|one\n' "$TS" >> "$V2_LOG"
printf '%s|bob|C:\\\\new\n' "$TS" >> "$V2_LOG"

V2_OUTPUT=$("$BIN" --log-check "$V2_LOG" 2>&1)
V2_STATUS=$?
if [ "$V2_STATUS" -eq 0 ] &&
   printf '%s\n' "$V2_OUTPUT" | grep -q '^format_version 2$' &&
   printf '%s\n' "$V2_OUTPUT" | grep -q '^records_seen 2$' &&
   printf '%s\n' "$V2_OUTPUT" | grep -q '^invalid_records 0$'; then
    pass "the header is accepted and is not counted as a record"
else
    fail "v2 log check"
    printf '%s\n' "$V2_OUTPUT"
    echo "exit status: $V2_STATUS"
fi

V1_LOG="$STATE_DIR/v1-backslash.log"
printf '%s|alice|C:\\new\n' "$TS" > "$V1_LOG"

V1_CHECK_OUTPUT=$("$BIN" --log-check "$V1_LOG" 2>&1)
if printf '%s\n' "$V1_CHECK_OUTPUT" | grep -q '^format_version 1$'; then
    pass "an unmigrated log reports v1"
else
    fail "v1 log check"
    printf '%s\n' "$V1_CHECK_OUTPUT"
fi

UPGRADED="$STATE_DIR/upgraded.log"
"$BIN" --log-recover "$V1_LOG" > "$UPGRADED" 2>/dev/null
if head -n 1 "$UPGRADED" | grep -qx '#tnt-message-log v2' &&
   grep -qF 'C:\\new' "$UPGRADED"; then
    pass "recover writes the header and escapes a v1 backslash"
else
    fail "v1 recovery"
    cat "$UPGRADED" 2>/dev/null
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
