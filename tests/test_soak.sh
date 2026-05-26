#!/bin/sh
# Lightweight soak test for TNT.
# Usage: ./test_soak.sh [duration_seconds] [reconnect_count]

PORT=${PORT:-2222}
DURATION=${1:-8}
RECONNECTS=${2:-5}
BIN="../tnt"
PASS=0
FAIL=0
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-soak-test.XXXXXX")
SERVER_PID=""
IDLE_PID=""

cleanup() {
    if [ -n "$IDLE_PID" ]; then
        kill "$IDLE_PID" 2>/dev/null || true
        wait "$IDLE_PID" 2>/dev/null || true
    fi
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$STATE_DIR"
}

trap cleanup EXIT

case "$DURATION" in
    ''|*[!0-9]*)
        echo "Error: duration_seconds must be a positive integer"
        exit 2
        ;;
esac

case "$RECONNECTS" in
    ''|*[!0-9]*)
        echo "Error: reconnect_count must be a positive integer"
        exit 2
        ;;
esac

if [ "$DURATION" -lt 1 ] || [ "$RECONNECTS" -lt 1 ]; then
    echo "Error: duration_seconds and reconnect_count must be positive"
    exit 2
fi

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

if ! command -v expect >/dev/null 2>&1; then
    echo "expect not installed; skipping soak test"
    exit 0
fi

SSH_OPTS="-n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -p $PORT"
SSH_TTY_OPTS="-e none -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -p $PORT"

wait_for_health() {
    out=""
    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
        if [ -n "$SERVER_PID" ] && ! kill -0 "$SERVER_PID" 2>/dev/null; then
            return 1
        fi
        out=$(ssh $SSH_OPTS localhost health 2>/dev/null || true)
        [ "$out" = "ok" ] && return 0
        sleep 1
    done
    return 1
}

echo "=== TNT Soak Test ==="
echo "duration=${DURATION}s reconnects=$RECONNECTS port=$PORT"

TNT_LANG=zh "$BIN" \
    --bind 127.0.0.1 \
    --public-host soak.local \
    --max-connections 32 \
    --max-conn-per-ip 32 \
    --max-conn-rate-per-ip 64 \
    --rate-limit 0 \
    --idle-timeout 0 \
    --ssh-log-level 1 \
    -p "$PORT" \
    -d "$STATE_DIR" >"$STATE_DIR/server.log" 2>&1 &
SERVER_PID=$!

if wait_for_health; then
    echo "✓ server started"
    PASS=$((PASS + 1))
else
    echo "✗ server failed to start"
    sed -n '1,160p' "$STATE_DIR/server.log"
    exit 1
fi

if grep -q 'ssh -p '"$PORT"' soak.local' "$STATE_DIR/server.log"; then
    echo "✓ explicit public host appears in startup hint"
    PASS=$((PASS + 1))
else
    echo "✗ explicit public host missing from startup hint"
    sed -n '1,80p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

IDLE_READY="$STATE_DIR/idle.ready"
cat >"$STATE_DIR/idle.expect" <<EOF
set timeout [expr {$DURATION + 20}]
spawn ssh $SSH_TTY_OPTS idle@127.0.0.1
sleep 1
send -- "soakidle\r"
expect "›"
exec touch "$IDLE_READY"
sleep $DURATION
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

expect "$STATE_DIR/idle.expect" >"$STATE_DIR/idle.log" 2>&1 &
IDLE_PID=$!

for _ in 1 2 3 4 5 6 7 8 9 10; do
    [ -f "$IDLE_READY" ] && break
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        break
    fi
    sleep 1
done

if [ -f "$IDLE_READY" ]; then
    echo "✓ idle interactive session reached chat"
    PASS=$((PASS + 1))
else
    echo "✗ idle interactive session did not reach chat"
    sed -n '1,160p' "$STATE_DIR/idle.log"
    FAIL=$((FAIL + 1))
fi

control_failed=0
for i in $(seq 1 "$DURATION"); do
    HEALTH=$(ssh $SSH_OPTS localhost health 2>/dev/null || true)
    STATS=$(ssh $SSH_OPTS localhost stats --json 2>/dev/null || true)
    USERS=$(ssh $SSH_OPTS localhost users --json 2>/dev/null || true)

    if [ "$HEALTH" != "ok" ] ||
       ! printf '%s\n' "$STATS" | grep -q '"status":"ok"' ||
       ! printf '%s\n' "$USERS" | grep -q 'soakidle'; then
        echo "✗ control interface failed during idle soak at ${i}s"
        printf 'health=%s\nstats=%s\nusers=%s\n' "$HEALTH" "$STATS" "$USERS"
        FAIL=$((FAIL + 1))
        control_failed=1
        break
    fi
    sleep 1
done

if [ "$control_failed" -eq 0 ]; then
    echo "✓ control interface stayed available during idle soak"
    PASS=$((PASS + 1))
fi

reconnected=0
for i in $(seq 1 "$RECONNECTS"); do
    cat >"$STATE_DIR/reconnect-$i.expect" <<EOF
set timeout 10
spawn ssh $SSH_TTY_OPTS reconnect$i@127.0.0.1
sleep 1
send -- "reconnect$i\r"
expect "›"
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF
    if expect "$STATE_DIR/reconnect-$i.expect" \
        >"$STATE_DIR/reconnect-$i.log" 2>&1; then
        reconnected=$((reconnected + 1))
    else
        sed -n '1,120p' "$STATE_DIR/reconnect-$i.log"
        break
    fi
done

if [ "$reconnected" -eq "$RECONNECTS" ]; then
    echo "✓ repeated reconnects completed"
    PASS=$((PASS + 1))
else
    echo "✗ repeated reconnects stopped at $reconnected/$RECONNECTS"
    FAIL=$((FAIL + 1))
fi

LAST_MESSAGE="soak message $RECONNECTS"
POST=$(ssh $SSH_OPTS soakbot@localhost post "$LAST_MESSAGE" 2>/dev/null || true)
TAIL=$(ssh $SSH_OPTS localhost "tail -n 5" 2>/dev/null || true)
if [ "$POST" = "posted" ] &&
   printf '%s\n' "$TAIL" | grep -q "$LAST_MESSAGE"; then
    echo "✓ post/tail path stayed available after reconnect churn"
    PASS=$((PASS + 1))
else
    echo "✗ post/tail path failed after reconnect churn"
    printf '%s\n' "$POST"
    printf '%s\n' "$TAIL"
    FAIL=$((FAIL + 1))
fi

wait "$IDLE_PID" 2>/dev/null || FAIL=$((FAIL + 1))
IDLE_PID=""

if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "✓ server survived soak test"
    PASS=$((PASS + 1))
else
    echo "✗ server exited during soak test"
    sed -n '1,160p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
