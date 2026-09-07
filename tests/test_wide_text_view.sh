#!/bin/sh
# Regression test: long messages wrap across rows instead of being truncated.

PORT=${PORT:-12351}
PASS=0
FAIL=0
BIN="../tnt"
SERVER_PID=""
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-wide-text-test.XXXXXX")

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$STATE_DIR"
}

trap cleanup EXIT

if ! command -v expect >/dev/null 2>&1; then
    echo "expect not installed; skipping wide text view test"
    exit 0
fi

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

SSH_OPTS="-e none -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectionAttempts=3 -o ConnectTimeout=15 -p $PORT"

echo "=== TNT Wide Text View Test ==="

# The seeded line is far wider than any terminal the test may get, so the
# assertions below do not depend on the negotiated terminal width.  TAILMARK
# only reaches the screen if the message wrapped; a truncating renderer drops
# it.
seed_ts=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
printf '%s|fixture|%s\n' "$seed_ts" \
    "😌 aaaaaaaaaa bbbbbbbbbb cccccccccc dddddddddd eeeeeeeeee ffffffffff gggggggggg hhhhhhhhhh iiiiiiiiii jjjjjjjjjj TAILMARK" \
    >>"$STATE_DIR/messages.log"
printf '%s|fixture|%s\n' "$seed_ts" "HEADMARK short line" \
    >>"$STATE_DIR/messages.log"

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

VIEW_SCRIPT="$STATE_DIR/wide-text-view.expect"
cat >"$VIEW_SCRIPT" <<EOF
set timeout 15
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "viewer\r"
expect "/help"
sleep 2
close
EOF

VIEW_OUTPUT=$(expect -f "$VIEW_SCRIPT" 2>&1)

if printf '%s' "$VIEW_OUTPUT" | grep -q "TAILMARK"; then
    echo "✓ the tail of a long message survives on a wrapped row"
    PASS=$((PASS + 1))
else
    echo "✗ long message tail is missing — it was truncated, not wrapped"
    printf '%s\n' "$VIEW_OUTPUT" | tail -20
    FAIL=$((FAIL + 1))
fi

if printf '%s' "$VIEW_OUTPUT" | grep -q "HEADMARK"; then
    echo "✓ neighbouring short messages still render"
    PASS=$((PASS + 1))
else
    echo "✗ short message disappeared"
    FAIL=$((FAIL + 1))
fi

if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "✓ server survived wide text rendering"
    PASS=$((PASS + 1))
else
    echo "✗ server died while rendering wide text"
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
