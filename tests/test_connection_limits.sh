#!/bin/sh
# Connection limit regression tests for TNT

PORT=${PORT:-2222}
BIN="../tnt"
PASS=0
FAIL=0
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-limit-test.XXXXXX")
SERVER_PID=""
WATCHER_PID=""

cleanup() {
    if [ -n "$WATCHER_PID" ]; then
        kill "$WATCHER_PID" 2>/dev/null || true
        wait "$WATCHER_PID" 2>/dev/null || true
    fi
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

SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -p $PORT"

wait_for_health() {
    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
        if [ -n "$SERVER_PID" ] && ! kill -0 "$SERVER_PID" 2>/dev/null; then
            return 1
        fi
        OUT=$(ssh $SSH_OPTS localhost health 2>/dev/null || true)
        [ "$OUT" = "ok" ] && return 0
        sleep 1
    done
    return 1
}

echo "=== TNT Connection Limit Tests ==="

TNT_RATE_LIMIT=0 TNT_MAX_CONN_PER_IP=1 "$BIN" -p "$PORT" -d "$STATE_DIR" \
    >"$STATE_DIR/concurrent.log" 2>&1 &
SERVER_PID=$!

if wait_for_health; then
    echo "✓ server started for concurrent limit test"
    PASS=$((PASS + 1))
else
    echo "✗ server failed to start for concurrent limit test"
    exit 1
fi

WATCHER_READY="$STATE_DIR/watcher.ready"
cat >"$STATE_DIR/watcher.expect" <<EOF
set timeout 10
spawn ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p $PORT watcher@localhost
expect "请输入用户名"
send "watcher\r"
exec touch "$WATCHER_READY"
sleep 8
send "\003"
expect eof
EOF

expect "$STATE_DIR/watcher.expect" >"$STATE_DIR/watcher.log" 2>&1 &
WATCHER_PID=$!

for _ in 1 2 3 4 5 6 7 8 9 10; do
    [ -f "$WATCHER_READY" ] && break
    sleep 1
done

if [ ! -f "$WATCHER_READY" ]; then
    echo "✗ watcher session did not become ready"
    sed -n '1,120p' "$STATE_DIR/watcher.log"
    exit 1
fi

if ssh $SSH_OPTS localhost health >/dev/null 2>&1; then
    echo "✗ concurrent per-IP limit was not enforced"
    FAIL=$((FAIL + 1))
else
    echo "✓ concurrent per-IP limit rejects a second session"
    PASS=$((PASS + 1))
fi

wait "$WATCHER_PID" 2>/dev/null || true
WATCHER_PID=""
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=""

RATE_PORT=$((PORT + 1))
SSH_RATE_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -p $RATE_PORT"

TNT_MAX_CONN_PER_IP=10 TNT_MAX_CONN_RATE_PER_IP=2 "$BIN" -p "$RATE_PORT" -d "$STATE_DIR" \
    >"$STATE_DIR/rate.log" 2>&1 &
SERVER_PID=$!

sleep 2
if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "✓ server started for rate limit test"
    PASS=$((PASS + 1))
else
    echo "✗ server failed to start for rate limit test"
    sed -n '1,120p' "$STATE_DIR/rate.log"
    exit 1
fi

R1=$(ssh $SSH_RATE_OPTS localhost health 2>/dev/null || true)
R2=$(ssh $SSH_RATE_OPTS localhost health 2>/dev/null || true)
if ssh $SSH_RATE_OPTS localhost health >/dev/null 2>&1; then
    echo "✗ per-IP connection-rate limit was not enforced"
    FAIL=$((FAIL + 1))
else
    if [ "$R1" = "ok" ] && [ "$R2" = "ok" ]; then
        echo "✓ per-IP connection-rate limit blocks after the configured burst"
        PASS=$((PASS + 1))
    else
        echo "✗ per-IP connection-rate limit setup failed unexpectedly"
        FAIL=$((FAIL + 1))
    fi
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
