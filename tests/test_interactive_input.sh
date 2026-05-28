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

USERNAME_CANCEL_SCRIPT="$STATE_DIR/username-cancel.expect"
cat >"$USERNAME_CANCEL_SCRIPT" <<EOF
set timeout 10
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "\003"
expect eof
EOF

if expect "$USERNAME_CANCEL_SCRIPT" >"$STATE_DIR/username-cancel.log" 2>&1; then
    echo "✓ Ctrl+C cancels before username join"
    PASS=$((PASS + 1))
else
    echo "x Ctrl+C before username failed"
    sed -n '1,120p' "$STATE_DIR/username-cancel.log"
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

USERNAME_EDIT_SCRIPT="$STATE_DIR/username-edit.expect"
cat >"$USERNAME_EDIT_SCRIPT" <<EOF
set timeout 10
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "wrong\025editeduser\r"
expect "Esc NORMAL"
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$USERNAME_EDIT_SCRIPT" >"$STATE_DIR/username-edit.log" 2>&1 &&
   grep -q 'editeduser' "$STATE_DIR/messages.log" &&
   ! grep -q 'wrongediteduser' "$STATE_DIR/messages.log"; then
    echo "✓ Ctrl+U edits username before join"
    PASS=$((PASS + 1))
else
    echo "x username line editing failed"
    sed -n '1,120p' "$STATE_DIR/username-edit.log" 2>/dev/null || true
    cat "$STATE_DIR/messages.log" 2>/dev/null || true
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

EXPECT_SCRIPT="$STATE_DIR/bracketed-paste.expect"
cat >"$EXPECT_SCRIPT" <<EOF
set timeout 10
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "tester\r"
expect "Esc NORMAL"
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

HELP_SCRIPT="$STATE_DIR/help.expect"
cat >"$HELP_SCRIPT" <<EOF
set timeout 10
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "helper\r"
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- ":"
expect ":"
send -- ":help\r"
expect "TNT\\(1\\) 帮助"
expect "Tab 补全 @mention"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- "?"
expect "TNT 按键参考"
expect "Tab        - 补全 @mention"
expect "l:语言"
send -- "\003"
expect "NORMAL"
send -- "?"
expect "TNT 按键参考"
send -- "l"
expect "TNT KEY REFERENCE"
expect "Complete @mention"
expect "l:lang"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "lang en\r"
expect "Language set to: en"
expect "q:close"
send -- "q"
sleep 0.2
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$HELP_SCRIPT" >"$STATE_DIR/help.log" 2>&1; then
    echo "✓ :help renders concise manual"
    PASS=$((PASS + 1))
else
    echo "x :help command failed"
    sed -n '1,160p' "$STATE_DIR/help.log"
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

HELP_PAGER_KEYS_SCRIPT="$STATE_DIR/help-pager-keys.expect"
cat >"$HELP_PAGER_KEYS_SCRIPT" <<EOF
set timeout 10
stty rows 8 columns 80
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "helppager\r"
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- "?"
expect -re {\(1/[2-9][0-9]*\)}
send -- "\033\[6~"
expect -re {\([2-9][0-9]*/[2-9][0-9]*\)}
send -- "\033\[5~"
expect -re {\(1/[2-9][0-9]*\)}
send -- "\033\[F"
expect -re {\([2-9][0-9]*/[2-9][0-9]*\)}
send -- "\033\[H"
expect -re {\(1/[2-9][0-9]*\)}
send -- "q"
expect "NORMAL"
sleep 0.2
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$HELP_PAGER_KEYS_SCRIPT" >"$STATE_DIR/help-pager-keys.log" 2>&1; then
    echo "✓ help pager accepts terminal paging keys"
    PASS=$((PASS + 1))
else
    echo "x help pager terminal keys failed"
    sed -n '1,220p' "$STATE_DIR/help-pager-keys.log"
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

UNKNOWN_SCRIPT="$STATE_DIR/unknown-command.expect"
cat >"$UNKNOWN_SCRIPT" <<EOF
set timeout 10
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "mistype\r"
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- ":"
expect ":"
send -- "hlep\r"
expect "你是想输入 :help 吗?"
expect "q:关闭"
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
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- ":"
expect ":"
send -- "mute-joins\r"
expect "命令输出"
expect "加入/离开提示"
expect "已静音"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "lang en\r"
expect "Language set to: en"
expect "q:close"
send -- "q"
expect "NORMAL"
expect "online"
send -- ":"
expect ":"
send -- "users\r"
expect "COMMAND OUTPUT"
expect "Online users"
expect "q:close"
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

COMMAND_USAGE_SCRIPT="$STATE_DIR/command-usage.expect"
cat >"$COMMAND_USAGE_SCRIPT" <<EOF
set timeout 10
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "usageuser\r"
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- ":"
expect ":"
send -- "search\r"
expect "用法: search <keyword>"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "msg\r"
expect "用法: msg <user> <message>"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "nick\r"
expect "用法: nick <name>"
expect "q:关闭"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "lang en\r"
expect "Language set to: en"
expect "q:close"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "inbox\r"
expect "Private messages"
expect "(empty)"
expect "r:refresh"
send -- "r"
expect "Private messages"
expect "q:close"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "last 999\r"
expect "Usage: last \\[N\\]"
expect "q:close"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "users extra\r"
expect "Usage: users"
expect "q:close"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "help now\r"
expect "Usage: help"
expect "q:close"
send -- "q"
sleep 0.2
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$COMMAND_USAGE_SCRIPT" >"$STATE_DIR/command-usage.log" 2>&1; then
    echo "✓ command usage errors follow session language"
    PASS=$((PASS + 1))
else
    echo "x localized command usage failed"
    sed -n '1,220p' "$STATE_DIR/command-usage.log"
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

scroll_ts=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
scroll_i=1
while [ "$scroll_i" -le 30 ]; do
    printf '%s|fixture|scroll fixture %02d\n' "$scroll_ts" "$scroll_i" >>"$STATE_DIR/messages.log"
    scroll_i=$((scroll_i + 1))
done

COMMAND_OUTPUT_SCROLL_SCRIPT="$STATE_DIR/command-output-scroll.expect"
cat >"$COMMAND_OUTPUT_SCROLL_SCRIPT" <<EOF
set timeout 10
stty rows 8 columns 80
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "pageruser\r"
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- ":"
expect ":"
send -- "last 50\r"
expect "j/k:滚动"
expect -re {\(1/[2-9][0-9]*\)}
send -- "j"
expect -re {\(2/[2-9][0-9]*\)}
send -- "\033\[6~"
expect -re {\([3-9][0-9]*/[2-9][0-9]*\)}
send -- "\033\[5~"
expect -re {\([1-9][0-9]*/[2-9][0-9]*\)}
send -- "\033\[F"
expect -re {\([2-9][0-9]*/[2-9][0-9]*\)}
send -- "\033\[H"
expect -re {\(1/[2-9][0-9]*\)}
send -- "q"
expect "NORMAL"
sleep 0.2
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$COMMAND_OUTPUT_SCROLL_SCRIPT" >"$STATE_DIR/command-output-scroll.log" 2>&1; then
    echo "✓ command output can scroll before closing"
    PASS=$((PASS + 1))
else
    echo "x command output scrolling failed"
    sed -n '1,220p' "$STATE_DIR/command-output-scroll.log"
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

COMMAND_INPUT_WRAP_SCRIPT="$STATE_DIR/command-input-wrap.expect"
cat >"$COMMAND_INPUT_WRAP_SCRIPT" <<EOF
set timeout 10
stty rows 10 columns 40
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "wrapcmd\r"
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- ":"
expect ":"
send -- "search aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaatail"
expect -re {<a+tail}
send -- "\003"
expect "NORMAL"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$COMMAND_INPUT_WRAP_SCRIPT" >"$STATE_DIR/command-input-wrap.log" 2>&1; then
    echo "✓ long command input stays on one status line"
    PASS=$((PASS + 1))
else
    echo "x long command input display failed"
    sed -n '1,220p' "$STATE_DIR/command-input-wrap.log"
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

SYSTEM_MESSAGES_SCRIPT="$STATE_DIR/system-messages.expect"
cat >"$SYSTEM_MESSAGES_SCRIPT" <<EOF
set timeout 10
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "systemuser\r"
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- ":"
expect ":"
send -- "lang en\r"
expect "Language set to: en"
expect "q:close"
send -- "q"
expect "NORMAL"
send -- ":"
expect ":"
send -- "nick systemuser2\r"
expect "Nickname changed: systemuser -> systemuser2"
expect "q:close"
send -- "q"
sleep 0.2
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$SYSTEM_MESSAGES_SCRIPT" >"$STATE_DIR/system-messages.log" 2>&1 &&
   grep -q 'system|systemuser renamed to systemuser2' "$STATE_DIR/messages.log" &&
   grep -q 'system|systemuser2 left the room' "$STATE_DIR/messages.log"; then
    echo "✓ system messages follow session language"
    PASS=$((PASS + 1))
else
    echo "x localized system messages failed"
    sed -n '1,220p' "$STATE_DIR/system-messages.log" 2>/dev/null || true
    cat "$STATE_DIR/messages.log" 2>/dev/null || true
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

printf '维护窗口\n' >"$STATE_DIR/motd.txt"
MOTD_SCRIPT="$STATE_DIR/motd.expect"
cat >"$MOTD_SCRIPT" <<EOF
set timeout 10
spawn ssh $SSH_OPTS anonymous@127.0.0.1
sleep 1
send -- "motduser\r"
expect "公告"
expect "维护窗口"
expect "按任意键继续"
send -- "x"
expect "INSERT"
sleep 0.2
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$MOTD_SCRIPT" >"$STATE_DIR/motd.log" 2>&1; then
    echo "✓ MOTD chrome follows session language"
    PASS=$((PASS + 1))
else
    echo "x localized MOTD chrome failed"
    sed -n '1,200p' "$STATE_DIR/motd.log"
    sed -n '1,120p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
