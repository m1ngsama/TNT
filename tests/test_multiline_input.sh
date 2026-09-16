#!/bin/sh
# End-to-end test: Ctrl+J and Alt+Enter compose extra lines and Enter sends
# them as one record.

. ./lib.sh

PORT=${PORT:-12362}
trap tnt_cleanup EXIT

tnt_skip_without_expect "multi-line input test"
tnt_require_binary
tnt_state_dir multiline-test

echo "=== TNT Multi-line Input Test ==="

tnt_start_server "$PORT" "$STATE_DIR"

# Assertions read messages.log rather than the screen: stty in an expect
# script does not reach the server's idea of the terminal size.
LOG="$STATE_DIR/messages.log"

# 1. Ctrl+J is the guaranteed newline key: it is a real 0x0A byte, which no
#    terminal sends for Enter in raw mode.
tnt_expect_session "liner" '
send_wait "first"
send_wait "\n"
send_wait "second"
send_enter
'

if tnt_wait_for_fixed "$LOG" '|liner|first\nsecond'; then
    pass "Ctrl+J composes a second line and Enter sends one record"
else
    fail "multi-line record not found"
    cat "$LOG" 2>/dev/null
fi

if [ "$(grep -c '|liner|' "$LOG" 2>/dev/null)" = "1" ]; then
    pass "the two lines are one record, not two"
else
    fail "expected exactly one record from liner"
    cat "$LOG" 2>/dev/null
fi

# 2. Alt+Enter is ESC followed by CR, and must not be mistaken for the plain
#    ESC that means NORMAL in the vim keymap.
tnt_expect_session "altliner" '
send_wait "top"
send_wait "\033\r"
send_wait "bottom"
send_enter
'

if tnt_wait_for_fixed "$LOG" '|altliner|top\nbottom'; then
    pass "Alt+Enter composes a second line in the default keymap"
else
    fail "Alt+Enter did not insert a newline"
    cat "$LOG" 2>/dev/null
fi

# 3. The record stays one physical line, so dump and tail keep their contract.
if [ "$(grep -c '^2' "$LOG")" = "$(grep -c '' "$LOG")" ] ||
   ! grep -q '^first$\|^second$\|^bottom$' "$LOG"; then
    pass "no record was split across physical lines"
else
    fail "a message body leaked onto its own line"
    cat "$LOG" 2>/dev/null
fi

if [ "$(head -n 1 "$LOG")" = "#tnt-message-log v2" ]; then
    pass "a fresh log is v2"
else
    fail "header missing"
    head -n 3 "$LOG" 2>/dev/null
fi

tnt_summary
