#!/bin/sh
# Lightweight concurrent-client stress test for TNT.
# Usage: ./test_stress.sh [num_clients] [duration_seconds]

PORT=${PORT:-2222}
CLIENTS=${1:-10}
DURATION=${2:-30}
BIN="../tnt"
PASS=0
FAIL=0
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-stress-test.XXXXXX")
SERVER_PID=""
CLIENT_PIDS=""

cleanup() {
    for pid in $CLIENT_PIDS; do
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$STATE_DIR"
}

trap cleanup EXIT

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

case "$CLIENTS" in
    ''|*[!0-9]*)
        echo "Error: num_clients must be a positive integer"
        exit 2
        ;;
esac

case "$DURATION" in
    ''|*[!0-9]*)
        echo "Error: duration_seconds must be a positive integer"
        exit 2
        ;;
esac

if [ "$CLIENTS" -lt 1 ] || [ "$DURATION" -lt 1 ]; then
    echo "Error: num_clients and duration_seconds must be positive"
    exit 2
fi

SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -p $PORT"

wait_for_health() {
    out=""
    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
        if [ -n "$SERVER_PID" ] && ! kill -0 "$SERVER_PID" 2>/dev/null; then
            return 1
        fi
        out=$(ssh -n $SSH_OPTS localhost health 2>/dev/null || true)
        [ "$out" = "ok" ] && return 0
        sleep 1
    done
    return 1
}

echo "=== TNT Stress Test ==="
echo "clients=$CLIENTS duration=${DURATION}s port=$PORT"

MAX_CONN_PER_IP=$((CLIENTS + 5))
TNT_LANG=zh TNT_RATE_LIMIT=0 TNT_MAX_CONN_PER_IP=$MAX_CONN_PER_IP \
    "$BIN" -p "$PORT" -d "$STATE_DIR" >"$STATE_DIR/server.log" 2>&1 &
SERVER_PID=$!

if wait_for_health; then
    echo "✓ server started"
    PASS=$((PASS + 1))
else
    echo "✗ server failed to start"
    sed -n '1,120p' "$STATE_DIR/server.log"
    exit 1
fi

for i in $(seq 1 "$CLIENTS"); do
    script="$STATE_DIR/client-$i.expect"
    log="$STATE_DIR/client-$i.log"
    ready="$STATE_DIR/client-$i.ready"

    cat >"$script" <<EOF
set timeout [expr {$DURATION + 15}]
spawn ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p $PORT stress$i@localhost
expect "请输入用户名"
send -- "stress$i\r"
exec touch "$ready"
sleep $DURATION
send -- "\003"
expect eof
EOF

    expect "$script" >"$log" 2>&1 &
    CLIENT_PIDS="$CLIENT_PIDS $!"
done

ready_count=0
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    ready_count=$(find "$STATE_DIR" -name 'client-*.ready' -type f | wc -l | tr -d ' ')
    [ "$ready_count" = "$CLIENTS" ] && break
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        break
    fi
    sleep 1
done

if [ "$ready_count" = "$CLIENTS" ]; then
    echo "✓ all clients reached chat"
    PASS=$((PASS + 1))
else
    echo "✗ only $ready_count/$CLIENTS clients reached chat"
    sed -n '1,160p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

for pid in $CLIENT_PIDS; do
    wait "$pid" 2>/dev/null || FAIL=$((FAIL + 1))
done
CLIENT_PIDS=""

if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "✓ server survived concurrent clients"
    PASS=$((PASS + 1))
else
    echo "✗ server exited during stress test"
    sed -n '1,160p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
