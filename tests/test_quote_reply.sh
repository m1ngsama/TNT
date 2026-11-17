#!/bin/sh
# End-to-end test: :quote puts a room message into the input line as a quote
# plus a mention, and a "> " line renders dimmed.

. ./lib.sh

PORT=${PORT:-12364}
trap tnt_cleanup EXIT

tnt_skip_without_expect "quote reply test"
tnt_require_binary
tnt_state_dir quote-test

echo "=== TNT Quote Reply Test ==="

TNT_KEYMAP=default
export TNT_KEYMAP
tnt_start_server "$PORT" "$STATE_DIR"

EXEC_OPTS="-n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
-o BatchMode=yes -o ConnectionAttempts=3 -o ConnectTimeout=15 -p $PORT"
LOG="$STATE_DIR/messages.log"

ssh $EXEC_OPTS alice@localhost post \
    'the release build is green on every target we ship, arm64 included' \
    >/dev/null 2>&1
ssh $EXEC_OPTS bob@localhost post 'nice' >/dev/null 2>&1
tnt_wait_for "$LOG" '|bob|nice' || {
    fail "seeded messages did not persist"
    tnt_summary
}

tnt_expect_session "carol" '
send_wait "/quote"
send -- "\r"
expect "@bob"
send_wait "ship it"
send -- "\r"
send_wait "/quote alice"
send -- "\r"
expect "@alice"
send_wait "agreed"
send -- "\r"
send_wait "/quote 2"
send -- "\r"
expect "@alice"
send_wait "again"
send -- "\r"
send_wait "/quote nobody"
send -- "\r"
expect "No room message to quote"
'

if tnt_wait_for_fixed "$LOG" '|carol|> bob: nice\n@bob ship it'; then
    pass ":quote with no argument quotes the newest message from someone else"
else
    fail "bare :quote did not produce the expected record"
    cat "$LOG" 2>/dev/null
    tnt_dump_session carol
fi

if grep -qF '|carol|> alice: the release build is green on every target we ship, arm64 i…\n@alice agreed' "$LOG"; then
    pass ":quote <user> quotes that user and shortens a long line to 60 columns"
else
    fail ":quote alice did not produce the expected record"
    cat "$LOG" 2>/dev/null
fi

if grep -qF '|carol|> alice: ' "$LOG" &&
   [ "$(grep -c '|carol|> alice: ' "$LOG")" = "2" ]; then
    pass ":quote <n> counts back over other people's messages"
else
    fail ":quote 2 did not quote alice"
    cat "$LOG" 2>/dev/null
fi

if grep -q 'nobody' "$LOG"; then
    fail ":quote of an unknown name posted something"
else
    pass ":quote of an unknown name reports and posts nothing"
fi

VIEW_SCRIPT="$STATE_DIR/quote-view.expect"
cat >"$VIEW_SCRIPT" <<EOF
set timeout 10
spawn ssh $(tnt_ssh_opts "$PORT") anonymous@127.0.0.1
expect "): "
send -- "viewer\r"
expect "again"
expect "/help"
close
EOF
VIEW=$(expect -f "$VIEW_SCRIPT" 2>&1)

if printf '%s' "$VIEW" | grep -qF "$(printf '\033[2m> bob: nice\033[22m')"; then
    pass "a quote line renders dimmed with its marker"
else
    fail "quote line was not dimmed"
    printf '%s\n' "$VIEW" | tail -20
fi

tnt_summary
