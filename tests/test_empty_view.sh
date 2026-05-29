#!/bin/sh
# Regression test for the empty/filtered-empty main view.

PORT=${PORT:-12350}
PASS=0
FAIL=0
BIN="../tnt"
SERVER_PID=""
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-empty-view-test.XXXXXX")

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$STATE_DIR"
}

trap cleanup EXIT

if ! command -v expect >/dev/null 2>&1; then
    echo "expect not installed; skipping empty view test"
    exit 0
fi

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

SSH_OPTS="-e none -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -p $PORT"

echo "=== TNT Empty View Test ==="

TNT_LANG=en TNT_RATE_LIMIT=0 "$BIN" --bind 127.0.0.1 \
    -p "$PORT" -d "$STATE_DIR" >"$STATE_DIR/server.log" 2>&1 &
SERVER_PID=$!

SERVER_READY=0
for _ in 1 2 3 4 5; do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "x Server failed to start"
        sed -n '1,120p' "$STATE_DIR/server.log"
        exit 1
    fi
    if grep -q "TNT chat server listening" "$STATE_DIR/server.log"; then
        SERVER_READY=1
        break
    fi
    sleep 1
done

if [ "$SERVER_READY" -eq 1 ]; then
    echo "✓ server started"
    PASS=$((PASS + 1))
else
    echo "x Server did not become ready"
    sed -n '1,120p' "$STATE_DIR/server.log"
    exit 1
fi

VIEW_SCRIPT="$STATE_DIR/empty-view.expect"
cat >"$VIEW_SCRIPT" <<EOF
set timeout 10
stty rows 10 columns 80
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "viewer\r"
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- ":"
expect ":"
send -- "mute-joins\r"
expect "Join/leave notifications"
expect "muted"
expect "q:close"
send -- "q"
expect "NORMAL"
expect "No visible messages"
send -- ":"
expect ":"
send -- "last 5\r"
expect "Last 0"
expect "No messages to show"
send -- "q"
expect "NORMAL"
sleep 0.2
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$VIEW_SCRIPT" >"$STATE_DIR/empty-view.log" 2>&1; then
    echo "✓ filtered-empty main view shows a state hint"
    PASS=$((PASS + 1))
else
    echo "x filtered-empty main view did not show state hint"
    sed -n '1,220p' "$STATE_DIR/empty-view.log"
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
