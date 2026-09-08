#!/bin/sh
# End-to-end test: Ctrl+J and Alt+Enter compose extra lines and Enter sends
# them as one record.

PORT=${PORT:-12362}
PASS=0
FAIL=0
BIN="../tnt"
SERVER_PID=""
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-multiline-test.XXXXXX")

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$STATE_DIR"
}
trap cleanup EXIT

pass() { echo "✓ $1"; PASS=$((PASS + 1)); }
fail() { echo "✗ $1"; FAIL=$((FAIL + 1)); }

if ! command -v expect >/dev/null 2>&1; then
    echo "expect not installed; skipping multi-line input test"
    exit 0
fi
if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

SSH_OPTS="-e none -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectionAttempts=3 -o ConnectTimeout=15 -p $PORT"

echo "=== TNT Multi-line Input Test ==="

TNT_LANG=en TNT_RATE_LIMIT=0 TNT_MAX_CONN_PER_IP=256 TNT_MAX_CONNECTIONS=256 \
    "$BIN" --bind 127.0.0.1 -p "$PORT" -d "$STATE_DIR" \
    >"$STATE_DIR/server.log" 2>&1 &
SERVER_PID=$!

SERVER_READY=0
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "✗ Server failed to start"
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
    pass "server started"
else
    echo "✗ Server did not become ready"
    sed -n '1,120p' "$STATE_DIR/server.log"
    exit 1
fi

# Assertions read messages.log rather than the screen: stty in an expect
# script does not reach the server's idea of the terminal size.
run_case() {
    name=$1
    keys=$2

    SCRIPT="$STATE_DIR/case.expect"
    cat >"$SCRIPT" <<EOF
set timeout 15
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "$name\r"
expect "/help"
$keys
sleep 2
close
EOF
    expect -f "$SCRIPT" >/dev/null 2>&1
    sleep 1
}

# 1. Ctrl+J is the guaranteed newline key: it is a real 0x0A byte, which no
#    terminal sends for Enter in raw mode.
run_case "liner" '
send -- "first"
sleep 1
send -- "\n"
sleep 1
send -- "second"
sleep 1
send -- "\r"
'

if grep -qF '|liner|first\nsecond' "$STATE_DIR/messages.log" 2>/dev/null; then
    pass "Ctrl+J composes a second line and Enter sends one record"
else
    fail "multi-line record not found"
    cat "$STATE_DIR/messages.log" 2>/dev/null
fi

if [ "$(grep -c '|liner|' "$STATE_DIR/messages.log" 2>/dev/null)" = "1" ]; then
    pass "the two lines are one record, not two"
else
    fail "expected exactly one record from liner"
    cat "$STATE_DIR/messages.log" 2>/dev/null
fi

# 2. Alt+Enter is ESC followed by CR, and must not be mistaken for the plain
#    ESC that means NORMAL in the vim keymap.
run_case "altliner" '
send -- "top"
sleep 1
send -- "\033\r"
sleep 1
send -- "bottom"
sleep 1
send -- "\r"
'

if grep -qF '|altliner|top\nbottom' "$STATE_DIR/messages.log" 2>/dev/null; then
    pass "Alt+Enter composes a second line in the default keymap"
else
    fail "Alt+Enter did not insert a newline"
    cat "$STATE_DIR/messages.log" 2>/dev/null
fi

# 3. The record stays one physical line, so dump and tail keep their contract.
if [ "$(grep -c '^2' "$STATE_DIR/messages.log")" = "$(grep -c '' "$STATE_DIR/messages.log")" ] ||
   ! grep -q '^first$\|^second$\|^bottom$' "$STATE_DIR/messages.log"; then
    pass "no record was split across physical lines"
else
    fail "a message body leaked onto its own line"
    cat "$STATE_DIR/messages.log" 2>/dev/null
fi

if [ "$(head -n 1 "$STATE_DIR/messages.log")" = "#tnt-message-log v2" ]; then
    pass "a fresh log is v2"
else
    fail "header missing"
    head -n 3 "$STATE_DIR/messages.log" 2>/dev/null
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
