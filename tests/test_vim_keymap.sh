#!/bin/sh
# Regression test for the vim keymap.
#
# Written before the default keymap was flipped, so it is a record of the
# behaviour that existed then.  It reaches vim mode through the `vim@` SSH
# login name, which works whichever way the server default points.

PORT=${PORT:-12354}
PASS=0
FAIL=0
BIN="../tnt"
SERVER_PID=""
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-vim-keymap-test.XXXXXX")

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$STATE_DIR"
}

trap cleanup EXIT

if ! command -v expect >/dev/null 2>&1; then
    echo "expect not installed; skipping vim keymap test"
    exit 0
fi

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

SSH_OPTS="-e none -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectionAttempts=3 -o ConnectTimeout=15 -p $PORT"

echo "=== TNT Vim Keymap Test ==="

TNT_LANG=en TNT_RATE_LIMIT=0 TNT_MAX_CONN_PER_IP=256 TNT_MAX_CONNECTIONS=256 "$BIN" --bind 127.0.0.1 \
    -p "$PORT" -d "$STATE_DIR" >"$STATE_DIR/server.log" 2>&1 &
SERVER_PID=$!

SERVER_READY=0
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
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

# Esc enters NORMAL, i returns to INSERT, and the message still sends.
MODES_SCRIPT="$STATE_DIR/modes.expect"
cat >"$MODES_SCRIPT" <<EOF
set timeout 15
spawn ssh $SSH_OPTS vim@127.0.0.1
expect "): "
send -- "vimuser\r"
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- "i"
expect "INSERT"
send -- "typed in insert"
sleep 1
send -- "\r"
sleep 2
close
EOF

MODES_OUTPUT=$(expect -f "$MODES_SCRIPT" 2>&1)

if printf '%s' "$MODES_OUTPUT" | grep -q "NORMAL"; then
    echo "✓ Esc enters NORMAL"
    PASS=$((PASS + 1))
else
    echo "✗ Esc did not enter NORMAL"
    printf '%s\n' "$MODES_OUTPUT" | tail -20
    FAIL=$((FAIL + 1))
fi

if grep -q "|typed in insert$" "$STATE_DIR/messages.log" 2>/dev/null; then
    echo "✓ i returns to INSERT and Enter still sends"
    PASS=$((PASS + 1))
else
    echo "✗ i did not return to INSERT"
    cat "$STATE_DIR/messages.log" 2>/dev/null
    FAIL=$((FAIL + 1))
fi

# ":" opens COMMAND and a command runs from there.
COMMAND_SCRIPT="$STATE_DIR/command.expect"
cat >"$COMMAND_SCRIPT" <<EOF
set timeout 15
spawn ssh $SSH_OPTS vim@127.0.0.1
expect "): "
send -- "vimcmd\r"
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- ":"
expect ":"
send -- "nick vimcmd2\r"
sleep 2
close
EOF

expect -f "$COMMAND_SCRIPT" >/dev/null 2>&1
sleep 1

if grep -q "vimcmd renamed to vimcmd2" "$STATE_DIR/messages.log" 2>/dev/null; then
    echo "✓ colon opens COMMAND and the command runs"
    PASS=$((PASS + 1))
else
    echo "✗ colon did not reach COMMAND mode"
    cat "$STATE_DIR/messages.log" 2>/dev/null
    FAIL=$((FAIL + 1))
fi

if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "✓ server survived the vim keymap session"
    PASS=$((PASS + 1))
else
    echo "✗ server died"
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"

if [ "$FAIL" -eq 0 ]; then
    echo "All tests passed"
    exit 0
fi
exit 1
