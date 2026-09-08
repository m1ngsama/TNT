#!/bin/sh
# End-to-end test: an existing v1 messages.log is migrated once, on startup.

PORT=${PORT:-12360}
PASS=0
FAIL=0
BIN="../tnt"
SERVER_PID=""
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-log-migration-test.XXXXXX")

LOG="$STATE_DIR/messages.log"
BACKUP="$STATE_DIR/messages.log.v1.bak"
SEED="$STATE_DIR/seed.log"

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    fi
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

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

SSH_OPTS="-n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -o ConnectionAttempts=3 -o ConnectTimeout=15 -p $PORT"

echo "=== TNT Message Log Migration Test ==="

start_server() {
    TNT_LANG=en TNT_RATE_LIMIT=0 TNT_MAX_CONN_PER_IP=256 TNT_MAX_CONNECTIONS=256 \
        "$BIN" --bind 127.0.0.1 -p "$PORT" -d "$STATE_DIR" \
        >"$STATE_DIR/server.log" 2>&1 &
    SERVER_PID=$!

    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo "✗ Server failed to start"
            sed -n '1,120p' "$STATE_DIR/server.log"
            exit 1
        fi
        [ "$(ssh $SSH_OPTS anonymous@127.0.0.1 health 2>/dev/null)" = "ok" ] && return 0
        sleep 1
    done

    echo "✗ Server did not become ready"
    sed -n '1,120p' "$STATE_DIR/server.log"
    exit 1
}

stop_server() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""
}

TS=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
printf '%s|alice|C:\\new\n' "$TS" > "$SEED"
printf '%s|bob|plain message\n' "$TS" >> "$SEED"
cp "$SEED" "$LOG"

start_server

if [ -f "$BACKUP" ] && cmp -s "$SEED" "$BACKUP"; then
    pass "the pre-migration log is backed up verbatim"
else
    fail "backup missing or altered"
fi

if [ "$(head -n 1 "$LOG")" = "#tnt-message-log v2" ]; then
    pass "the migrated log carries the header"
else
    fail "header missing"
    sed -n '1,5p' "$LOG"
fi

if grep -qF '|alice|C:\\new' "$LOG" && grep -qF '|bob|plain message' "$LOG"; then
    pass "the backslash record is escaped and the plain record is untouched"
else
    fail "migrated content unexpected"
    sed -n '1,5p' "$LOG"
fi

DUMP=$(ssh $SSH_OPTS anonymous@127.0.0.1 "dump --all" 2>/dev/null)
if printf '%s\n' "$DUMP" | grep -qF '|alice|C:\\new' &&
   [ "$(printf '%s\n' "$DUMP" | wc -l | tr -d ' ')" = "2" ]; then
    pass "dump emits the escaped form, one record per line"
else
    fail "dump output unexpected"
    printf '%s\n' "$DUMP"
fi

stop_server
BEFORE=$(cat "$LOG")
BACKUP_BEFORE=$(cat "$BACKUP")

start_server

if [ "$(cat "$LOG")" = "$BEFORE" ] && [ "$(cat "$BACKUP")" = "$BACKUP_BEFORE" ]; then
    pass "a second start changes nothing"
else
    fail "second start rewrote the log"
    sed -n '1,5p' "$LOG"
fi

stop_server

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
