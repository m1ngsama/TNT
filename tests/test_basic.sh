#!/bin/sh
# Basic functional tests
# Usage: ./test_basic.sh

PORT=${PORT:-2222}
PASS=0
FAIL=0
BIN="../tnt"
SERVER_PID=""
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-basic-test.XXXXXX")
SSH_HEALTH_OPTS="-n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -p $PORT"

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$STATE_DIR"
}

trap cleanup EXIT

wait_for_health() {
    out=""
    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
        if [ -n "$SERVER_PID" ] && ! kill -0 "$SERVER_PID" 2>/dev/null; then
            return 1
        fi
        out=$(ssh $SSH_HEALTH_OPTS localhost health 2>/dev/null || true)
        [ "$out" = "ok" ] && return 0
        sleep 1
    done
    return 1
}

echo "=== TNT Basic Tests ==="

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

if ! command -v expect >/dev/null 2>&1; then
    echo "expect not installed; skipping basic interactive tests"
    exit 0
fi

# Start server
"$BIN" -p "$PORT" -d "$STATE_DIR" >"$STATE_DIR/server.log" 2>&1 &
SERVER_PID=$!

# Test 1: Server started and accepts exec health checks
if wait_for_health; then
    echo "✓ Server started"
    PASS=$((PASS + 1))
else
    echo "✗ Server failed to start"
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
    exit 1
fi

# Test 2: SSH connection
CONNECT_SCRIPT="$STATE_DIR/connect.expect"
cat >"$CONNECT_SCRIPT" <<EOF
set timeout 10
spawn ssh -e none -p $PORT -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR anonymous@127.0.0.1
sleep 1
send -- "basic\r"
expect "›"
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$CONNECT_SCRIPT" >"$STATE_DIR/connect.log" 2>&1; then
    echo "✓ SSH connection works"
    PASS=$((PASS + 1))
else
    echo "✗ SSH connection failed"
    sed -n '1,120p' "$STATE_DIR/connect.log"
    FAIL=$((FAIL + 1))
fi

# Test 3: Message logging
MESSAGE_SCRIPT="$STATE_DIR/message.expect"
cat >"$MESSAGE_SCRIPT" <<EOF
set timeout 10
spawn ssh -e none -p $PORT -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR anonymous@127.0.0.1
sleep 1
send -- "testuser\r"
expect "›"
send -- "test message\r"
sleep 1
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$MESSAGE_SCRIPT" >"$STATE_DIR/message.log.out" 2>&1 &&
   grep -q 'testuser|test message' "$STATE_DIR/messages.log"; then
    echo "✓ Message logging works"
    PASS=$((PASS + 1))
else
    echo "✗ Message logging failed"
    sed -n '1,120p' "$STATE_DIR/message.log.out"
    cat "$STATE_DIR/messages.log" 2>/dev/null || true
    FAIL=$((FAIL + 1))
fi

# Summary
echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ $FAIL -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit $FAIL
