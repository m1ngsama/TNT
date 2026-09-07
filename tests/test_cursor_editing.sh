#!/bin/sh
# Regression test: the message line is edited at the cursor, and an arrow key
# never discards what the user is composing.

PORT=${PORT:-12352}
PASS=0
FAIL=0
BIN="../tnt"
SERVER_PID=""
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-cursor-edit-test.XXXXXX")

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$STATE_DIR"
}

trap cleanup EXIT

if ! command -v expect >/dev/null 2>&1; then
    echo "expect not installed; skipping cursor editing test"
    exit 0
fi

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

SSH_OPTS="-e none -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectionAttempts=3 -o ConnectTimeout=15 -p $PORT"

echo "=== TNT Cursor Editing Test ==="

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

# Each case types a line, moves the caret, edits, and sends.  The assertions
# read messages.log rather than scraping the screen, so they do not depend on
# the negotiated terminal width.
run_case() {
    case_name=$1
    keys=$2

    SCRIPT="$STATE_DIR/case.expect"
    cat >"$SCRIPT" <<EOF
set timeout 15
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "editor\r"
expect "Esc NORMAL"
$keys
sleep 2
close
EOF
    expect -f "$SCRIPT" >/dev/null 2>&1
    sleep 1
}

# 1. The reported defect: Left arrow used to leave INSERT mode, so Enter
#    discarded the message entirely.
run_case "left-arrow" '
send -- "helo world"
sleep 1
send -- "\033\[D\033\[D\033\[D\033\[D\033\[D\033\[D\033\[D"
sleep 1
send -- "l"
sleep 1
send -- "\r"
'

if grep -q "hello world" "$STATE_DIR/messages.log" 2>/dev/null; then
    echo "✓ left arrow edits in place instead of discarding the message"
    PASS=$((PASS + 1))
else
    echo "✗ message was lost or not edited at the cursor"
    cat "$STATE_DIR/messages.log" 2>/dev/null
    FAIL=$((FAIL + 1))
fi

# 2. Home puts the caret at the start of the line.
run_case "home" '
send -- "abc"
sleep 1
send -- "\033\[H"
sleep 1
send -- "X"
sleep 1
send -- "\r"
'

if grep -q "Xabc" "$STATE_DIR/messages.log" 2>/dev/null; then
    echo "✓ Home moves the caret to the start of the line"
    PASS=$((PASS + 1))
else
    echo "✗ Home did not move the caret"
    cat "$STATE_DIR/messages.log" 2>/dev/null
    FAIL=$((FAIL + 1))
fi

# 3. Delete removes the character under the caret.
run_case "delete" '
send -- "abZc"
sleep 1
send -- "\033\[D\033\[D"
sleep 1
send -- "\033\[3~"
sleep 1
send -- "\r"
'

if grep -q "|abc$" "$STATE_DIR/messages.log" 2>/dev/null; then
    echo "✓ Delete removes the character under the caret"
    PASS=$((PASS + 1))
else
    echo "✗ Delete did not remove the character under the caret"
    cat "$STATE_DIR/messages.log" 2>/dev/null
    FAIL=$((FAIL + 1))
fi

if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "✓ server survived cursor editing"
    PASS=$((PASS + 1))
else
    echo "✗ server died during cursor editing"
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
