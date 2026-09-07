#!/bin/sh
# Regression test for the non-modal default keymap: typing always types, and
# commands come from a leading slash.

PORT=${PORT:-12353}
PASS=0
FAIL=0
BIN="../tnt"
SERVER_PID=""
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-default-keymap-test.XXXXXX")

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$STATE_DIR"
}

trap cleanup EXIT

if ! command -v expect >/dev/null 2>&1; then
    echo "expect not installed; skipping default keymap test"
    exit 0
fi

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

SSH_OPTS="-e none -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectionAttempts=3 -o ConnectTimeout=15 -p $PORT"

echo "=== TNT Default Keymap Test ==="

# Explicitly ask for the default keymap so this test states what it exercises
# rather than depending on whichever default the build happens to carry.
TNT_LANG=en TNT_KEYMAP=default TNT_RATE_LIMIT=0 TNT_MAX_CONN_PER_IP=256 TNT_MAX_CONNECTIONS=256 "$BIN" --bind 127.0.0.1 \
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

run_case() {
    keys=$1
    SCRIPT="$STATE_DIR/case.expect"
    cat >"$SCRIPT" <<EOF
set timeout 15
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "plain\r"
expect "/help"
$keys
sleep 2
close
EOF
    expect -f "$SCRIPT" >/dev/null 2>&1
    sleep 1
}

# 1. A colon is an ordinary character here; it must not open a command line.
run_case '
send -- ":list"
sleep 1
send -- "\r"
'
if grep -q "|:list$" "$STATE_DIR/messages.log" 2>/dev/null; then
    echo "✓ a colon types instead of opening COMMAND"
    PASS=$((PASS + 1))
else
    echo "✗ the colon was treated as a command prefix"
    cat "$STATE_DIR/messages.log" 2>/dev/null
    FAIL=$((FAIL + 1))
fi

# 2. Esc must not change modes: the keys after it still type.
run_case '
send -- "\033"
sleep 1
send -- "after-escape"
sleep 1
send -- "\r"
'
if grep -q "|after-escape$" "$STATE_DIR/messages.log" 2>/dev/null; then
    echo "✓ Esc does not leave the input"
    PASS=$((PASS + 1))
else
    echo "✗ Esc changed modes, so the following keys were lost"
    cat "$STATE_DIR/messages.log" 2>/dev/null
    FAIL=$((FAIL + 1))
fi

# 3. A leading slash runs a command.
run_case '
send -- "/me waves"
sleep 1
send -- "\r"
'
if grep -q "|\*|plain waves$" "$STATE_DIR/messages.log" 2>/dev/null; then
    echo "✓ a leading slash runs the command"
    PASS=$((PASS + 1))
else
    echo "✗ /me was not dispatched as a command"
    cat "$STATE_DIR/messages.log" 2>/dev/null
    FAIL=$((FAIL + 1))
fi

# 4. A doubled slash sends a literal one, so ordinary text starting with "/"
#    is still reachable.
run_case '
send -- "//slash"
sleep 1
send -- "\r"
'
if grep -q "|/slash$" "$STATE_DIR/messages.log" 2>/dev/null; then
    echo "✓ a doubled slash sends a literal slash"
    PASS=$((PASS + 1))
else
    echo "✗ // did not produce a literal slash"
    cat "$STATE_DIR/messages.log" 2>/dev/null
    FAIL=$((FAIL + 1))
fi

# 5. An unknown slash word is ordinary text, so a path still sends.
run_case '
send -- "/usr/local/bin is where it lives"
sleep 1
send -- "\r"
'
if grep -q "|/usr/local/bin is where it lives$" "$STATE_DIR/messages.log" 2>/dev/null; then
    echo "✓ an unknown slash word sends as text"
    PASS=$((PASS + 1))
else
    echo "✗ a path starting with / was swallowed"
    cat "$STATE_DIR/messages.log" 2>/dev/null
    FAIL=$((FAIL + 1))
fi

if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "✓ server survived the default keymap session"
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
