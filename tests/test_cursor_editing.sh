#!/bin/sh
# Regression test: the message line is edited at the cursor, and an arrow key
# never discards what the user is composing.

. ./lib.sh

PORT=${PORT:-12352}
trap tnt_cleanup EXIT

tnt_skip_without_expect "cursor editing test"
tnt_require_binary
tnt_state_dir cursor-edit-test

echo "=== TNT Cursor Editing Test ==="

tnt_start_server "$PORT" "$STATE_DIR"

# Each case types a line, moves the caret, edits, and sends.  The assertions
# read messages.log rather than scraping the screen, so they do not depend on
# the negotiated terminal width.
LOG="$STATE_DIR/messages.log"

check_case() {
    if tnt_wait_for "$LOG" "$1"; then
        pass "$2"
    else
        fail "$3"
        cat "$LOG" 2>/dev/null
    fi
}

# 1. The reported defect: Left arrow used to leave INSERT mode, so Enter
#    discarded the message entirely.
tnt_expect_session "left-arrow" '
send_wait "helo world"
send_repeat "\033\[D" 7
send_wait "l"
send_enter
'
check_case "hello world" \
    "left arrow edits in place instead of discarding the message" \
    "message was lost or not edited at the cursor"

# 2. Home puts the caret at the start of the line.
tnt_expect_session "home" '
send_wait "abc"
send_wait "\033\[H"
send_wait "X"
send_enter
'
check_case "Xabc" \
    "Home moves the caret to the start of the line" \
    "Home did not move the caret"

# 3. Delete removes the character under the caret.
tnt_expect_session "delete" '
send_wait "abZc"
send_repeat "\033\[D" 2
send_wait "\033\[3~"
send_enter
'
check_case "|abc$" \
    "Delete removes the character under the caret" \
    "Delete did not remove the character under the caret"

tnt_check_server_alive "server survived cursor editing"

tnt_summary
