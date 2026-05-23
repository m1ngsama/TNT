#!/bin/sh
# Interactive input regression tests for TNT.

PORT=${PORT:-12347}
PASS=0
FAIL=0
BIN="../tnt"
SERVER_PID=""
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-input-test.XXXXXX")

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$STATE_DIR"
}

trap cleanup EXIT

if ! command -v expect >/dev/null 2>&1; then
    echo "expect not installed; skipping interactive input tests"
    exit 0
fi

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

SSH_OPTS="-e none -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -p $PORT"

echo "=== TNT Interactive Input Tests ==="

TNT_LANG=zh TNT_RATE_LIMIT=0 "$BIN" -p "$PORT" -d "$STATE_DIR" >"$STATE_DIR/server.log" 2>&1 &
SERVER_PID=$!

SERVER_READY=0
for _ in 1 2 3 4 5; do
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

EXPECT_SCRIPT="$STATE_DIR/bracketed-paste.expect"
cat >"$EXPECT_SCRIPT" <<EOF
set timeout 10
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "tester\r"
expect ":support"
send -- "\033\[200~"
send -- "line1\nline2\nline3"
send -- "\033\[201~"
sleep 1
send -- "\r"
sleep 1
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$EXPECT_SCRIPT" >"$STATE_DIR/expect.log" 2>&1; then
    if grep -q 'tester|line1 line2 line3' "$STATE_DIR/messages.log" &&
       ! grep -q 'tester|line1$' "$STATE_DIR/messages.log"; then
        echo "✓ bracketed paste becomes one message"
        PASS=$((PASS + 1))
    else
        echo "x bracketed paste message log unexpected"
        cat "$STATE_DIR/messages.log" 2>/dev/null || true
        FAIL=$((FAIL + 1))
    fi
else
    echo "x bracketed paste client failed"
    sed -n '1,120p' "$STATE_DIR/expect.log"
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

LONG_SCRIPT="$STATE_DIR/long-paste.expect"
cat >"$LONG_SCRIPT" <<EOF
set timeout 10
set payload [string repeat a 1100]
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "longer\r"
expect "›"
send -- "\033\[200~"
send -- \$payload
send -- "\033\[201~"
sleep 1
send -- "\r"
sleep 1
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$LONG_SCRIPT" >"$STATE_DIR/long-paste.log" 2>&1; then
    long_line=$(grep 'longer|' "$STATE_DIR/messages.log" | tail -1)
    content=${long_line#*|}
    content=${content#*|}
    content_len=$(printf '%s' "$content" | wc -c | tr -d ' ')
    if [ "$content_len" -eq 1023 ]; then
        echo "✓ overlong paste is capped at message limit"
        PASS=$((PASS + 1))
    else
        echo "x overlong paste length unexpected: $content_len"
        FAIL=$((FAIL + 1))
    fi
else
    echo "x overlong paste client failed"
    sed -n '1,120p' "$STATE_DIR/long-paste.log"
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

SUPPORT_SCRIPT="$STATE_DIR/support.expect"
cat >"$SUPPORT_SCRIPT" <<EOF
set timeout 10
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "supporter\r"
expect ":support"
send -- "\033"
expect "NORMAL"
send -- ":"
expect ":"
send -- "support\r"
expect "支持"
expect "按任意键"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "lang en\r"
expect "Language set to: en"
expect "Press any key"
send -- "q"
sleep 0.2
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$SUPPORT_SCRIPT" >"$STATE_DIR/support.log" 2>&1; then
    echo "✓ :support renders quick guide"
    PASS=$((PASS + 1))
else
    echo "x :support command failed"
    sed -n '1,160p' "$STATE_DIR/support.log"
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

UNKNOWN_SCRIPT="$STATE_DIR/unknown-command.expect"
cat >"$UNKNOWN_SCRIPT" <<EOF
set timeout 10
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "mistype\r"
expect ":support"
send -- "\033"
expect "NORMAL"
send -- ":"
expect ":"
send -- "suport\r"
expect "你是想输入 :support 吗?"
expect "按任意键"
send -- "q"
sleep 0.2
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$UNKNOWN_SCRIPT" >"$STATE_DIR/unknown-command.log" 2>&1; then
    echo "✓ mistyped command suggests nearest command"
    PASS=$((PASS + 1))
else
    echo "x mistyped command suggestion failed"
    sed -n '1,160p' "$STATE_DIR/unknown-command.log"
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

LOCALIZED_COMMANDS_SCRIPT="$STATE_DIR/localized-commands.expect"
cat >"$LOCALIZED_COMMANDS_SCRIPT" <<EOF
set timeout 10
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "localized\r"
expect ":support"
send -- "\033"
expect "NORMAL"
send -- ":"
expect ":"
send -- "mute-joins\r"
expect "加入/离开提示"
expect "已静音"
expect "按任意键"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "lang en\r"
expect "Language set to: en"
expect "Press any key"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "users\r"
expect "Online users"
expect "Press any key"
send -- "q"
sleep 0.2
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$LOCALIZED_COMMANDS_SCRIPT" >"$STATE_DIR/localized-commands.log" 2>&1; then
    echo "✓ common command output follows session language"
    PASS=$((PASS + 1))
else
    echo "x localized command output failed"
    sed -n '1,200p' "$STATE_DIR/localized-commands.log"
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
