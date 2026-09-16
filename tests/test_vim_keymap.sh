#!/bin/sh
# Regression test for the vim keymap reached by logging in as vim@host.

. ./lib.sh

PORT=${PORT:-12354}
trap tnt_cleanup EXIT

tnt_skip_without_expect "vim keymap test"
tnt_require_binary
tnt_state_dir vim-keymap-test

echo "=== TNT Vim Keymap Test ==="

tnt_start_server "$PORT" "$STATE_DIR"

LOG="$STATE_DIR/messages.log"
SSH_OPTS=$(tnt_ssh_opts "$PORT")

# Esc enters NORMAL, i returns to INSERT, and the message still sends.
MODES_SCRIPT="$STATE_DIR/modes.expect"
cat >"$MODES_SCRIPT" <<EOF
set timeout 15
source "$PWD/lib.exp"
spawn ssh $SSH_OPTS vim@127.0.0.1
expect "): "
send -- "vimuser\r"
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- "i"
expect "INSERT"
send_wait "typed in insert"
send_enter
close
EOF

MODES_OUTPUT=$(expect -f "$MODES_SCRIPT" 2>&1)

if printf '%s' "$MODES_OUTPUT" | grep -q "NORMAL"; then
    pass "Esc enters NORMAL"
else
    fail "Esc did not enter NORMAL"
    printf '%s\n' "$MODES_OUTPUT" | tail -20
fi

if tnt_wait_for "$LOG" "|typed in insert$"; then
    pass "i returns to INSERT and Enter still sends"
else
    fail "i did not return to INSERT"
    cat "$LOG" 2>/dev/null
fi

# ":" opens COMMAND and a command runs from there.
COMMAND_SCRIPT="$STATE_DIR/command.expect"
cat >"$COMMAND_SCRIPT" <<EOF
set timeout 15
spawn ssh $SSH_OPTS vim@127.0.0.1
expect "): "
send -- "vimcmd\r"
expect "Esc NORMAL"
send -- "\033"
expect "NORMAL"
send -- ":"
expect ":"
send -- "nick vimcmd2\r"
expect "Nickname changed"
close
EOF

expect -f "$COMMAND_SCRIPT" >/dev/null 2>&1

if tnt_wait_for "$LOG" "vimcmd renamed to vimcmd2"; then
    pass "colon opens COMMAND and the command runs"
else
    fail "colon did not reach COMMAND mode"
    cat "$LOG" 2>/dev/null
fi

tnt_check_server_alive "server survived the vim keymap session"

tnt_summary
