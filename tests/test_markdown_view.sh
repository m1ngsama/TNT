#!/bin/sh
# Regression test: the markdown subset is rendered server-side, the markers
# never reach the screen, and the log keeps the text the author typed.

. ./lib.sh

PORT=${PORT:-12363}
trap tnt_cleanup EXIT

tnt_skip_without_expect "markdown view test"
tnt_require_binary
tnt_state_dir markdown-test

echo "=== TNT Markdown View Test ==="

tnt_start_server "$PORT" "$STATE_DIR"

SSH_OPTS=$(tnt_ssh_opts "$PORT")
EXEC_OPTS="-n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
-o BatchMode=yes -o ConnectionAttempts=3 -o ConnectTimeout=15 -p $PORT"
LOG="$STATE_DIR/messages.log"

# Seeded over exec so the assertions read a fresh client's first render rather
# than the sender's echo.
for line in \
    'a **big** deal' \
    'run `make test` now' \
    'see https://tnt.example/x now' \
    '**never closed'
do
    ssh $EXEC_OPTS author@localhost post "$line" >/dev/null 2>&1
done

tnt_wait_for "$LOG" 'never closed' || {
    fail "seeded messages did not persist"
    tnt_summary
}

VIEW_SCRIPT="$STATE_DIR/markdown-view.expect"
cat >"$VIEW_SCRIPT" <<EOF
set timeout 10
spawn ssh $SSH_OPTS anonymous@127.0.0.1
expect "): "
send -- "viewer\r"
expect "never closed"
expect "/help"
close
EOF
VIEW=$(expect -f "$VIEW_SCRIPT" 2>&1)

screen_has() {
    printf '%s' "$VIEW" | grep -qF "$(printf "$1")"
}

if screen_has '\033[1mbig\033[22m'; then
    pass "bold renders and its markers do not reach the screen"
else
    fail "bold was not rendered"
    printf '%s\n' "$VIEW" | tail -20
fi

if screen_has '\033[36mmake test\033[39m'; then
    pass "inline code renders"
else
    fail "inline code was not rendered"
fi

if screen_has '\033[4mhttps://tnt.example/x\033[24m'; then
    pass "a bare URL is underlined"
else
    fail "URL was not highlighted"
fi

if printf '%s' "$VIEW" | grep -q '\*\*never closed'; then
    pass "an unclosed marker stays literal"
else
    fail "unclosed marker was swallowed"
fi

if printf '%s' "$VIEW" | grep -q '\*\*big\*\*'; then
    fail "bold markers leaked to the screen"
else
    pass "no marker leaked into the rendered row"
fi

# The log is the author's text, not the render: dump and tail keep working and
# a future renderer change cannot rewrite history.
if grep -qF 'a **big** deal' "$LOG" && grep -qF 'run `make test` now' "$LOG"; then
    pass "the log keeps the markers the author typed"
else
    fail "the log was rewritten by the renderer"
    cat "$LOG" 2>/dev/null
fi

tnt_check_server_alive "server survived markdown rendering"

tnt_summary
