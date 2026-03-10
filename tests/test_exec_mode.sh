#!/bin/sh
# Exec-mode regression tests for TNT

PORT=${PORT:-2222}
PASS=0
FAIL=0
BIN="../tnt"
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-exec-test.XXXXXX")
INTERACTIVE_PID=""

cleanup() {
    if [ -n "${INTERACTIVE_PID}" ]; then
        kill "${INTERACTIVE_PID}" 2>/dev/null || true
        wait "${INTERACTIVE_PID}" 2>/dev/null || true
    fi
    kill "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
    rm -rf "${STATE_DIR}"
}

trap cleanup EXIT

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -p $PORT"

echo "=== TNT Exec Mode Tests ==="

TNT_RATE_LIMIT=0 $BIN -p "$PORT" -d "$STATE_DIR" >"${STATE_DIR}/server.log" 2>&1 &
SERVER_PID=$!

HEALTH_OUTPUT=""
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "✗ Server failed to start"
        exit 1
    fi
    HEALTH_OUTPUT=$(ssh $SSH_OPTS localhost health 2>/dev/null || true)
    [ "$HEALTH_OUTPUT" = "ok" ] && break
    sleep 1
done

if [ "$HEALTH_OUTPUT" = "ok" ]; then
    echo "✓ health returns ok"
    PASS=$((PASS + 1))
else
    echo "✗ health failed: $HEALTH_OUTPUT"
    FAIL=$((FAIL + 1))
fi

STATS_OUTPUT=$(ssh $SSH_OPTS localhost stats 2>/dev/null || true)
printf '%s\n' "$STATS_OUTPUT" | grep -q '^status ok$' &&
printf '%s\n' "$STATS_OUTPUT" | grep -q '^online_users 0$'
if [ $? -eq 0 ]; then
    echo "✓ stats returns key/value output"
    PASS=$((PASS + 1))
else
    echo "✗ stats output unexpected"
    printf '%s\n' "$STATS_OUTPUT"
    FAIL=$((FAIL + 1))
fi

STATS_JSON=$(ssh $SSH_OPTS localhost stats --json 2>/dev/null || true)
printf '%s\n' "$STATS_JSON" | grep -q '"status":"ok"' &&
printf '%s\n' "$STATS_JSON" | grep -q '"online_users":0'
if [ $? -eq 0 ]; then
    echo "✓ stats --json returns JSON"
    PASS=$((PASS + 1))
else
    echo "✗ stats --json output unexpected"
    printf '%s\n' "$STATS_JSON"
    FAIL=$((FAIL + 1))
fi

POST_OUTPUT=$(ssh $SSH_OPTS execposter@localhost post "hello from exec" 2>/dev/null || true)
if [ "$POST_OUTPUT" = "posted" ]; then
    echo "✓ post publishes a message"
    PASS=$((PASS + 1))
else
    echo "✗ post failed: $POST_OUTPUT"
    FAIL=$((FAIL + 1))
fi

TAIL_OUTPUT=$(ssh $SSH_OPTS localhost "tail -n 1" 2>/dev/null || true)
printf '%s\n' "$TAIL_OUTPUT" | grep -q 'execposter' &&
printf '%s\n' "$TAIL_OUTPUT" | grep -q 'hello from exec'
if [ $? -eq 0 ]; then
    echo "✓ tail returns recent messages"
    PASS=$((PASS + 1))
else
    echo "✗ tail output unexpected"
    printf '%s\n' "$TAIL_OUTPUT"
    FAIL=$((FAIL + 1))
fi

EXPECT_SCRIPT="${STATE_DIR}/watcher.expect"
WATCHER_READY="${STATE_DIR}/watcher.ready"
cat >"$EXPECT_SCRIPT" <<EOF
set timeout 10
spawn ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p $PORT watcher@localhost
expect "请输入用户名"
send "watcher\r"
exec touch "$WATCHER_READY"
sleep 8
send "\003"
expect eof
EOF

expect "$EXPECT_SCRIPT" >"${STATE_DIR}/expect.log" 2>&1 &
INTERACTIVE_PID=$!

for _ in 1 2 3 4 5 6 7 8 9 10; do
    [ -f "$WATCHER_READY" ] && break
    sleep 1
done

USERS_OUTPUT=""
for _ in 1 2 3 4 5; do
    USERS_OUTPUT=$(ssh $SSH_OPTS localhost users 2>/dev/null || true)
    printf '%s\n' "$USERS_OUTPUT" | grep -q '^watcher$' && break
    sleep 1
done

printf '%s\n' "$USERS_OUTPUT" | grep -q '^watcher$'
if [ $? -eq 0 ]; then
    echo "✓ users lists active interactive clients"
    PASS=$((PASS + 1))
else
    echo "✗ users output unexpected"
    printf '%s\n' "$USERS_OUTPUT"
    [ -f "$WATCHER_READY" ] || echo "watcher readiness marker was not created"
    [ -f "${STATE_DIR}/expect.log" ] && sed -n '1,120p' "${STATE_DIR}/expect.log"
    sed -n '1,120p' "${STATE_DIR}/server.log"
    FAIL=$((FAIL + 1))
fi

USERS_JSON=""
for _ in 1 2 3 4 5; do
    USERS_JSON=$(ssh $SSH_OPTS localhost users --json 2>/dev/null || true)
    printf '%s\n' "$USERS_JSON" | grep -q '"watcher"' && break
    sleep 1
done

printf '%s\n' "$USERS_JSON" | grep -q '"watcher"'
if [ $? -eq 0 ]; then
    echo "✓ users --json returns JSON array"
    PASS=$((PASS + 1))
else
    echo "✗ users --json output unexpected"
    printf '%s\n' "$USERS_JSON"
    [ -f "$WATCHER_READY" ] || echo "watcher readiness marker was not created"
    [ -f "${STATE_DIR}/expect.log" ] && sed -n '1,120p' "${STATE_DIR}/expect.log"
    sed -n '1,120p' "${STATE_DIR}/server.log"
    FAIL=$((FAIL + 1))
fi

wait "${INTERACTIVE_PID}" 2>/dev/null || true
INTERACTIVE_PID=""

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
