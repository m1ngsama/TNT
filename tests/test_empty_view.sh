#!/bin/sh
# Regression test for the empty/filtered-empty main view.

. ./lib.sh

PORT=${PORT:-12350}
trap tnt_cleanup EXIT

tnt_skip_without_expect "empty view test"
tnt_require_binary
tnt_state_dir empty-view-test

echo "=== TNT Empty View Test ==="

TNT_KEYMAP=vim
export TNT_KEYMAP
tnt_start_server "$PORT" "$STATE_DIR"

SSH_OPTS=$(tnt_ssh_opts "$PORT")

VIEW_SCRIPT="$STATE_DIR/empty-view.expect"
cat >"$VIEW_SCRIPT" <<EOF
set timeout 10
stty rows 10 columns 80
spawn ssh $SSH_OPTS anonymous@127.0.0.1
expect "): "
send -- "viewer\r"
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- ":"
expect ":"
send -- "mute-joins\r"
expect "Join/leave notifications"
expect "muted"
expect "q:close"
send -- "q"
expect "NORMAL"
expect "No visible messages"
send -- ":"
expect ":"
send -- "last 5\r"
expect "Last 0"
expect "No messages to show"
send -- "q"
expect "NORMAL"
sleep 0.2
send -- "\003"
sleep 0.2
send -- "\003"
expect eof
EOF

if expect "$VIEW_SCRIPT" >"$STATE_DIR/empty-view.log" 2>&1; then
    pass "filtered-empty main view shows a state hint"
else
    fail "filtered-empty main view did not show state hint"
    sed -n '1,220p' "$STATE_DIR/empty-view.log"
    sed -n '1,120p' "$STATE_DIR/server.log"
fi

tnt_summary
