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

SSH_OPTS="-n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -p $PORT"
TNTCTL_OPTS="--host-key-checking no --known-hosts /dev/null"

echo "=== TNT Exec Mode Tests ==="

TNT_LANG=zh TNT_RATE_LIMIT=0 $BIN -p "$PORT" -d "$STATE_DIR" >"${STATE_DIR}/server.log" 2>&1 &
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

HEALTH_USAGE=$(ssh $SSH_OPTS localhost health extra 2>/dev/null)
HEALTH_USAGE_STATUS=$?
printf '%s\n' "$HEALTH_USAGE" | grep -q '^health: 用法: health$'
if [ $? -eq 0 ] && [ "$HEALTH_USAGE_STATUS" -eq 64 ]; then
    echo "✓ no-arg exec usage follows TNT_LANG and exits 64"
    PASS=$((PASS + 1))
else
    echo "✗ no-arg exec usage output unexpected"
    printf '%s\n' "$HEALTH_USAGE"
    echo "exit status: $HEALTH_USAGE_STATUS"
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

HELP_OUTPUT=$(ssh $SSH_OPTS localhost help 2>/dev/null || true)
printf '%s\n' "$HELP_OUTPUT" | grep -q '^TNT exec 接口$' &&
printf '%s\n' "$HELP_OUTPUT" | grep -q '^命令:$'
if [ $? -eq 0 ]; then
    echo "✓ help follows TNT_LANG"
    PASS=$((PASS + 1))
else
    echo "✗ help output unexpected"
    printf '%s\n' "$HELP_OUTPUT"
    FAIL=$((FAIL + 1))
fi

UNKNOWN_OUTPUT=$(ssh $SSH_OPTS localhost nope 2>/dev/null)
UNKNOWN_STATUS=$?
printf '%s\n' "$UNKNOWN_OUTPUT" | grep -q '^未知命令: nope$'
if [ $? -eq 0 ] && [ "$UNKNOWN_STATUS" -eq 64 ]; then
    echo "✓ unknown command follows TNT_LANG and exits 64"
    PASS=$((PASS + 1))
else
    echo "✗ unknown command output unexpected"
    printf '%s\n' "$UNKNOWN_OUTPUT"
    echo "exit status: $UNKNOWN_STATUS"
    FAIL=$((FAIL + 1))
fi

POST_USAGE=$(ssh $SSH_OPTS localhost post 2>/dev/null)
POST_USAGE_STATUS=$?
printf '%s\n' "$POST_USAGE" | grep -q '^post: 用法: post MESSAGE$'
if [ $? -eq 0 ] && [ "$POST_USAGE_STATUS" -eq 64 ]; then
    echo "✓ post usage follows TNT_LANG and exits 64"
    PASS=$((PASS + 1))
else
    echo "✗ post usage output unexpected"
    printf '%s\n' "$POST_USAGE"
    echo "exit status: $POST_USAGE_STATUS"
    FAIL=$((FAIL + 1))
fi

USERS_USAGE=$(ssh $SSH_OPTS localhost users --xml 2>/dev/null)
USERS_USAGE_STATUS=$?
printf '%s\n' "$USERS_USAGE" | grep -q '^users: 用法: users \[--json\]$'
if [ $? -eq 0 ] && [ "$USERS_USAGE_STATUS" -eq 64 ]; then
    echo "✓ users usage follows TNT_LANG and exits 64"
    PASS=$((PASS + 1))
else
    echo "✗ users usage output unexpected"
    printf '%s\n' "$USERS_USAGE"
    echo "exit status: $USERS_USAGE_STATUS"
    FAIL=$((FAIL + 1))
fi

DUMP_USAGE=$(ssh $SSH_OPTS localhost "dump -n nope" 2>/dev/null)
DUMP_USAGE_STATUS=$?
printf '%s\n' "$DUMP_USAGE" | grep -q '^dump: 用法: dump \[N\] | dump -n N$'
if [ $? -eq 0 ] && [ "$DUMP_USAGE_STATUS" -eq 64 ]; then
    echo "✓ dump usage follows TNT_LANG and exits 64"
    PASS=$((PASS + 1))
else
    echo "✗ dump usage output unexpected"
    printf '%s\n' "$DUMP_USAGE"
    echo "exit status: $DUMP_USAGE_STATUS"
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

DUMP_OUTPUT=$(ssh $SSH_OPTS localhost "dump -n 1" 2>/dev/null || true)
printf '%s\n' "$DUMP_OUTPUT" | grep -q '|execposter|hello from exec$'
if [ $? -eq 0 ]; then
    echo "✓ dump returns persisted message log records"
    PASS=$((PASS + 1))
else
    echo "✗ dump output unexpected"
    printf '%s\n' "$DUMP_OUTPUT"
    FAIL=$((FAIL + 1))
fi

PERSIST_FAIL_MARKER="persist-fail-marker"
rm -f "$STATE_DIR/messages.log"
mkdir "$STATE_DIR/messages.log"
PERSIST_FAIL_OUTPUT=$(ssh $SSH_OPTS execposter@localhost post "$PERSIST_FAIL_MARKER" 2>/dev/null)
PERSIST_FAIL_STATUS=$?
rmdir "$STATE_DIR/messages.log"
printf '%s\n' "$PERSIST_FAIL_OUTPUT" | grep -q 'posted'
PERSIST_FAIL_POSTED=$?
PERSIST_FAIL_TAIL=$(ssh $SSH_OPTS localhost "tail -n 5" 2>/dev/null || true)
printf '%s\n' "$PERSIST_FAIL_TAIL" | grep -q "$PERSIST_FAIL_MARKER"
PERSIST_FAIL_VISIBLE=$?
if [ "$PERSIST_FAIL_STATUS" -eq 1 ] &&
   [ "$PERSIST_FAIL_POSTED" -ne 0 ] &&
   [ "$PERSIST_FAIL_VISIBLE" -ne 0 ]; then
    echo "✓ post persistence failure is not broadcast or acknowledged"
    PASS=$((PASS + 1))
else
    echo "✗ post persistence failure handling unexpected"
    printf '%s\n' "$PERSIST_FAIL_OUTPUT"
    printf '%s\n' "$PERSIST_FAIL_TAIL"
    echo "exit status: $PERSIST_FAIL_STATUS"
    FAIL=$((FAIL + 1))
fi

LONG_MARKER="too-long-exec-marker"
LONG_COMMAND=$(printf 'post %s %01020d' "$LONG_MARKER" 0)
LONG_OUTPUT=$(ssh $SSH_OPTS localhost "$LONG_COMMAND" 2>/dev/null)
LONG_STATUS=$?
printf '%s\n' "$LONG_OUTPUT" | grep -q '命令过长'
LONG_ERROR=$?
LONG_TAIL=$(ssh $SSH_OPTS localhost "tail -n 5" 2>/dev/null || true)
printf '%s\n' "$LONG_TAIL" | grep -q "$LONG_MARKER"
LONG_VISIBLE=$?
if [ "$LONG_STATUS" -eq 64 ] &&
   [ "$LONG_ERROR" -eq 0 ] &&
   [ "$LONG_VISIBLE" -ne 0 ]; then
    echo "✓ overlong exec command is rejected without truncation"
    PASS=$((PASS + 1))
else
    echo "✗ overlong exec command handling unexpected"
    printf '%s\n' "$LONG_OUTPUT"
    printf '%s\n' "$LONG_TAIL"
    echo "exit status: $LONG_STATUS"
    FAIL=$((FAIL + 1))
fi

TNTCTL_HEALTH=$("../tntctl" -p "$PORT" $TNTCTL_OPTS localhost health 2>/dev/null || true)
if [ "$TNTCTL_HEALTH" = "ok" ]; then
    echo "✓ tntctl health uses exec interface"
    PASS=$((PASS + 1))
else
    echo "✗ tntctl health failed: $TNTCTL_HEALTH"
    FAIL=$((FAIL + 1))
fi

TNTCTL_STATS=$("../tntctl" -p "$PORT" $TNTCTL_OPTS localhost stats --json 2>/dev/null || true)
printf '%s\n' "$TNTCTL_STATS" | grep -q '"status":"ok"'
if [ $? -eq 0 ]; then
    echo "✓ tntctl stats --json returns JSON"
    PASS=$((PASS + 1))
else
    echo "✗ tntctl stats --json output unexpected"
    printf '%s\n' "$TNTCTL_STATS"
    FAIL=$((FAIL + 1))
fi

TNTCTL_USERS_USAGE=$("../tntctl" -p "$PORT" $TNTCTL_OPTS localhost users --xml 2>/dev/null)
TNTCTL_USERS_STATUS=$?
printf '%s\n' "$TNTCTL_USERS_USAGE" | grep -q '^users: 用法: users \[--json\]$'
if [ $? -eq 0 ] && [ "$TNTCTL_USERS_STATUS" -eq 64 ]; then
    echo "✓ tntctl preserves remote usage exit 64"
    PASS=$((PASS + 1))
else
    echo "✗ tntctl users usage output unexpected"
    printf '%s\n' "$TNTCTL_USERS_USAGE"
    echo "exit status: $TNTCTL_USERS_STATUS"
    FAIL=$((FAIL + 1))
fi

TNTCTL_POST=$("../tntctl" -p "$PORT" $TNTCTL_OPTS -l ctlposter localhost post "hello from tntctl" 2>/dev/null || true)
if [ "$TNTCTL_POST" = "posted" ]; then
    echo "✓ tntctl post publishes a message"
    PASS=$((PASS + 1))
else
    echo "✗ tntctl post failed: $TNTCTL_POST"
    FAIL=$((FAIL + 1))
fi

TNTCTL_TAIL=$("../tntctl" -p "$PORT" $TNTCTL_OPTS localhost "tail" "-n" "1" 2>/dev/null || true)
printf '%s\n' "$TNTCTL_TAIL" | grep -q 'ctlposter' &&
printf '%s\n' "$TNTCTL_TAIL" | grep -q 'hello from tntctl'
if [ $? -eq 0 ]; then
    echo "✓ tntctl tail returns recent messages"
    PASS=$((PASS + 1))
else
    echo "✗ tntctl tail output unexpected"
    printf '%s\n' "$TNTCTL_TAIL"
    FAIL=$((FAIL + 1))
fi

TNTCTL_DUMP=$("../tntctl" -p "$PORT" $TNTCTL_OPTS localhost "dump" "-n" "1" 2>/dev/null || true)
printf '%s\n' "$TNTCTL_DUMP" | grep -q '|ctlposter|hello from tntctl$'
if [ $? -eq 0 ]; then
    echo "✓ tntctl dump returns persisted message log records"
    PASS=$((PASS + 1))
else
    echo "✗ tntctl dump output unexpected"
    printf '%s\n' "$TNTCTL_DUMP"
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
sleep 12
send "\003"
expect eof
EOF

expect "$EXPECT_SCRIPT" >"${STATE_DIR}/expect.log" 2>&1 &
INTERACTIVE_PID=$!

for _ in 1 2 3 4 5; do
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
for _ in 1 2 3 4 5 6 7 8 9 10; do
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

MENTION_OUTPUT=$(ssh $SSH_OPTS execposter@localhost post "@watcher hello from exec mention" 2>/dev/null || true)
if [ "$MENTION_OUTPUT" = "posted" ]; then
    echo "✓ post returns while notifying an interactive mention target"
    PASS=$((PASS + 1))
else
    echo "✗ mention post failed: $MENTION_OUTPUT"
    FAIL=$((FAIL + 1))
fi

MSG_SCRIPT="${STATE_DIR}/private-message.expect"
cat >"$MSG_SCRIPT" <<EOF
set timeout 10
spawn ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p $PORT sender@localhost
expect "请输入用户名"
send "sender\r"
expect "Esc NORMAL"
send "\033"
expect "NORMAL"
send ":"
expect ":"
send "msg watcher hello from private message\r"
expect "私信已发送给 watcher"
expect "q:关闭"
send "q"
sleep 0.2
send "\003"
expect eof
EOF

if expect "$MSG_SCRIPT" >"${STATE_DIR}/private-message.log" 2>&1; then
    echo "✓ :msg returns while queuing recipient notification"
    PASS=$((PASS + 1))
else
    echo "✗ :msg notification path failed"
    sed -n '1,120p' "${STATE_DIR}/private-message.log"
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
