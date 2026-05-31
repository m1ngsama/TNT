#!/bin/sh
# End-to-end user lifecycle test for TNT's interactive TUI.

PORT=${PORT:-2222}
BIN="../tnt"
PASS=0
FAIL=0
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-lifecycle-test.XXXXXX")
SERVER_PID=""
BOB_PID=""

cleanup() {
    if [ -n "$BOB_PID" ]; then
        kill "$BOB_PID" 2>/dev/null || true
        wait "$BOB_PID" 2>/dev/null || true
    fi
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$STATE_DIR"
}

trap cleanup EXIT

if ! command -v expect >/dev/null 2>&1; then
    echo "expect not installed; skipping user lifecycle test"
    exit 0
fi

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

SSH_OPTS="-e none -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -p $PORT"
SSH_EXEC_OPTS="-n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -p $PORT"
BOB_READY="$STATE_DIR/bob.ready"
PRIVATE_SENT="$STATE_DIR/private.sent"
REPLY_SENT="$STATE_DIR/reply.sent"

wait_for_health() {
    out=""
    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
        if [ -n "$SERVER_PID" ] && ! kill -0 "$SERVER_PID" 2>/dev/null; then
            return 1
        fi
        out=$(ssh $SSH_EXEC_OPTS localhost health 2>/dev/null || true)
        [ "$out" = "ok" ] && return 0
        sleep 1
    done
    return 1
}

echo "=== TNT User Lifecycle Test ==="

TNT_LANG=zh "$BIN" \
    --bind 127.0.0.1 \
    --public-host lifecycle.local \
    --max-connections 32 \
    --max-conn-per-ip 32 \
    --max-conn-rate-per-ip 64 \
    --rate-limit 0 \
    --idle-timeout 0 \
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

cat >"$STATE_DIR/bob.expect" <<EOF
set timeout 30
spawn ssh $SSH_OPTS bob@127.0.0.1
sleep 1
send -- "bob\r"
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- ":"
expect ":"
send -- "inbox\r"
expect "私信"
expect "(空)"
expect "r:刷新"
exec touch "$BOB_READY"
exec sh -c "while \[ ! -f '$PRIVATE_SENT' \]; do sleep 1; done"
expect "私信"
expect "alice"
expect "private lifecycle second"
expect "private lifecycle first"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "reply private lifecycle reply\r"
expect "私信已发送给 alice"
exec touch "$REPLY_SENT"
expect "q:关闭"
send -- "q"
expect "NORMAL"
sleep 0.2
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

expect "$STATE_DIR/bob.expect" >"$STATE_DIR/bob.log" 2>&1 &
BOB_PID=$!

for _ in 1 2 3 4 5 6 7 8 9 10; do
    [ -f "$BOB_READY" ] && break
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        break
    fi
    sleep 1
done

if [ -f "$BOB_READY" ]; then
    echo "✓ second user reached chat"
    PASS=$((PASS + 1))
else
    echo "✗ second user did not reach chat"
    sed -n '1,180p' "$STATE_DIR/bob.log"
    FAIL=$((FAIL + 1))
fi

USERS_JSON=""
for _ in 1 2 3 4 5; do
    USERS_JSON=$(ssh $SSH_EXEC_OPTS localhost users --json 2>/dev/null || true)
    printf '%s\n' "$USERS_JSON" | grep -q '"bob"' && break
    sleep 1
done
if printf '%s\n' "$USERS_JSON" | grep -q '"bob"'; then
    echo "✓ exec users sees active TUI user"
    PASS=$((PASS + 1))
else
    echo "✗ exec users did not see active TUI user"
    printf '%s\n' "$USERS_JSON"
    FAIL=$((FAIL + 1))
fi

cat >"$STATE_DIR/alice.expect" <<EOF
set timeout 30
spawn ssh $SSH_OPTS alice@127.0.0.1
sleep 1
send -- "alice\r"
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- "?"
expect "TNT 按键参考"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "users\r"
expect "在线用户"
expect "alice"
expect "bob"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- "i"
expect "Esc NORMAL"
send -- "hello lifecycle alpha\r"
sleep 1
send -- "\033"
expect "NORMAL"
send -- "k"
sleep 0.2
send -- "G"
expect "NORMAL"
send -- ":"
expect ":"
send -- "last 5\r"
expect "最近"
expect "hello lifecycle alpha"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "search alpha\r"
expect "搜索"
expect "alpha"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- "/alpha\r"
expect "搜索"
expect "alpha"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "mute-joins\r"
expect "加入/离开提示"
expect "已静音"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "msg bob private lifecycle first\r"
expect "私信已发送给 bob"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "msg bob private lifecycle second\r"
expect "私信已发送给 bob"
exec touch "$PRIVATE_SENT"
expect "q:关闭"
send -- "q"
expect "NORMAL"
exec sh -c "while \[ ! -f '$REPLY_SENT' \]; do sleep 1; done"
send -- ":"
expect ":"
send -- "inbox\r"
expect "bob"
expect "private lifecycle reply"
expect "你 -> bob"
expect "private lifecycle second"
expect "private lifecycle first"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "inbox clear\r"
expect "私信已清空"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "reply should not send after clear\r"
expect "没有可回复的私信"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "nick alice2\r"
expect "昵称已修改: alice -> alice2"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- "i"
expect "Esc NORMAL"
send -- "/me ships lifecycle\r"
sleep 1
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$STATE_DIR/alice.expect" >"$STATE_DIR/alice.log" 2>&1; then
    echo "✓ primary user lifecycle completed"
    PASS=$((PASS + 1))
else
    echo "✗ primary user lifecycle failed"
    sed -n '1,240p' "$STATE_DIR/alice.log"
    FAIL=$((FAIL + 1))
    touch "$PRIVATE_SENT"
fi

if wait "$BOB_PID" 2>/dev/null; then
    echo "✓ recipient inbox auto-refreshed after private message"
    PASS=$((PASS + 1))
else
    echo "✗ recipient inbox journey failed"
    sed -n '1,240p' "$STATE_DIR/bob.log"
    FAIL=$((FAIL + 1))
fi
BOB_PID=""

if grep -q '.*alice.*private lifecycle second' "$STATE_DIR/bob.log" &&
   grep -Eq '私信.*[0-9]+ 新' "$STATE_DIR/bob.log" &&
   grep -q '\*.*alice.*private lifecycle second' "$STATE_DIR/bob.log" &&
   grep -Eq '私信.*[0-9]+ 新' "$STATE_DIR/alice.log" &&
   grep -q '\*.*bob.*private lifecycle reply' "$STATE_DIR/alice.log"; then
    echo "✓ unread private messages are visibly marked in inbox"
    PASS=$((PASS + 1))
else
    echo "✗ inbox unread marker missing"
    sed -n '1,220p' "$STATE_DIR/bob.log"
    sed -n '1,260p' "$STATE_DIR/alice.log"
    FAIL=$((FAIL + 1))
fi

TAIL_OUTPUT=$(ssh $SSH_EXEC_OPTS localhost "tail -n 10" 2>/dev/null || true)
printf '%s\n' "$TAIL_OUTPUT" | grep -q 'hello lifecycle alpha' &&
printf '%s\n' "$TAIL_OUTPUT" | grep -q 'alice2 ships lifecycle'
if [ $? -eq 0 ]; then
    echo "✓ exec tail sees public lifecycle messages"
    PASS=$((PASS + 1))
else
    echo "✗ exec tail missing lifecycle messages"
    printf '%s\n' "$TAIL_OUTPUT"
    FAIL=$((FAIL + 1))
fi

if grep -q 'alice|hello lifecycle alpha' "$STATE_DIR/messages.log" &&
   grep -q '系统|alice 更名为 alice2' "$STATE_DIR/messages.log" &&
   grep -q '*|alice2 ships lifecycle' "$STATE_DIR/messages.log" &&
   ! grep -q 'private lifecycle ping' "$STATE_DIR/messages.log"; then
    echo "✓ persisted history matches public/private boundary"
    PASS=$((PASS + 1))
else
    echo "✗ persisted history boundary unexpected"
    cat "$STATE_DIR/messages.log" 2>/dev/null || true
    FAIL=$((FAIL + 1))
fi

if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "✓ server survived user lifecycle"
    PASS=$((PASS + 1))
else
    echo "✗ server exited during user lifecycle"
    sed -n '1,160p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
