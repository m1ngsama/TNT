#!/bin/sh
# Maintenance-script regression tests for scripts/logrotate.sh.

set -u

PASS=0
FAIL=0
SCRIPT="../scripts/logrotate.sh"
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-logrotate-test.XXXXXX")

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

archive_payload() {
    archive=$1
    case "$archive" in
        *.gz) gzip -cd "$archive" ;;
        *) cat "$archive" ;;
    esac
}

echo "=== TNT Logrotate Tests ==="

if [ ! -x "$SCRIPT" ]; then
    echo "Error: script $SCRIPT not found or not executable."
    exit 1
fi

MISSING_OUTPUT=$("$SCRIPT" "$STATE_DIR/missing.log" 100 10 2>&1)
MISSING_STATUS=$?
printf '%s\n' "$MISSING_OUTPUT" | grep -q 'does not exist'
if [ "$MISSING_STATUS" -eq 0 ] && [ $? -eq 0 ]; then
    pass "missing log is a successful no-op"
else
    fail "missing log handling"
    printf '%s\n' "$MISSING_OUTPUT"
fi

LOG="$STATE_DIR/messages.log"
cat > "$LOG" <<'EOF'
2026-01-01T00:00:01Z|alice|one
2026-01-01T00:00:02Z|bob|two
2026-01-01T00:00:03Z|carol|three
EOF

if "$SCRIPT" "$LOG" 100 2 >/dev/null 2>&1 &&
   grep -q 'alice|one' "$LOG" &&
   [ "$(ls "$LOG".* 2>/dev/null | wc -l | tr -d ' ')" -eq 0 ]; then
    pass "small log stays unmodified"
else
    fail "small log no-op"
    cat "$LOG" 2>/dev/null
fi

ROTATE_OUTPUT=$("$SCRIPT" "$LOG" 0 2 2>&1)
ROTATE_STATUS=$?
ARCHIVE=$(ls "$LOG".*.gz "$LOG".[0-9]* 2>/dev/null | head -n 1)
if [ "$ROTATE_STATUS" -eq 0 ] &&
   printf '%s\n' "$ROTATE_OUTPUT" | grep -q 'kept last 2 lines' &&
   ! grep -q 'alice|one' "$LOG" &&
   grep -q 'bob|two' "$LOG" &&
   grep -q 'carol|three' "$LOG" &&
   [ -n "$ARCHIVE" ] &&
   archive_payload "$ARCHIVE" | grep -q 'alice|one'; then
    pass "oversize log is archived and compacted"
else
    fail "oversize rotation"
    printf '%s\n' "$ROTATE_OUTPUT"
    cat "$LOG" 2>/dev/null
fi

DRY_LOG="$STATE_DIR/dry.log"
printf 'line1\nline2\nline3\n' > "$DRY_LOG"
DRY_BEFORE=$(cat "$DRY_LOG")
DRY_OUTPUT=$("$SCRIPT" --dry-run "$DRY_LOG" 0 1 2>&1)
DRY_STATUS=$?
if [ "$DRY_STATUS" -eq 0 ] &&
   [ "$(cat "$DRY_LOG")" = "$DRY_BEFORE" ] &&
   printf '%s\n' "$DRY_OUTPUT" | grep -q 'would archive'; then
    pass "dry run does not modify the log"
else
    fail "dry run handling"
    printf '%s\n' "$DRY_OUTPUT"
fi

INVALID_OUTPUT=$("$SCRIPT" "$LOG" nope 2 2>&1)
INVALID_STATUS=$?
if [ "$INVALID_STATUS" -eq 64 ] &&
   printf '%s\n' "$INVALID_OUTPUT" | grep -q 'invalid max size'; then
    pass "invalid arguments exit 64"
else
    fail "invalid argument status"
    printf '%s\n' "$INVALID_OUTPUT"
    echo "exit status: $INVALID_STATUS"
fi

DIR_OUTPUT=$("$SCRIPT" "$STATE_DIR" 0 1 2>&1)
DIR_STATUS=$?
if [ "$DIR_STATUS" -eq 1 ] &&
   printf '%s\n' "$DIR_OUTPUT" | grep -q 'not a regular file'; then
    pass "non-regular log path is rejected"
else
    fail "non-regular path handling"
    printf '%s\n' "$DIR_OUTPUT"
    echo "exit status: $DIR_STATUS"
fi

RET_LOG="$STATE_DIR/retention.log"
printf 'a\nb\nc\n' > "$RET_LOG"
printf old1 > "$RET_LOG.20000101T000000Z.gz"
sleep 1
printf old2 > "$RET_LOG.20010101T000000Z.gz"
sleep 1
printf old3 > "$RET_LOG.20020101T000000Z.gz"

if "$SCRIPT" --keep-archives 2 "$RET_LOG" 100 2 >/dev/null 2>&1 &&
   [ "$(ls "$RET_LOG".*.gz 2>/dev/null | wc -l | tr -d ' ')" -eq 2 ]; then
    pass "archive retention removes older archives"
else
    fail "archive retention"
    ls "$RET_LOG".* 2>/dev/null || true
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
