#!/bin/sh
# Regression test for the non-modal default keymap: typing always types, and
# commands come from a leading slash.

. ./lib.sh

PORT=${PORT:-12353}
trap tnt_cleanup EXIT

tnt_skip_without_expect "default keymap test"
tnt_require_binary
tnt_state_dir default-keymap-test

echo "=== TNT Default Keymap Test ==="

# Explicitly ask for the default keymap so this test states what it exercises
# rather than depending on whichever default the build happens to carry.
TNT_KEYMAP=default
export TNT_KEYMAP
tnt_start_server "$PORT" "$STATE_DIR"

LOG="$STATE_DIR/messages.log"

check_case() {
    if tnt_wait_for "$LOG" "$1"; then
        pass "$2"
    else
        fail "$3"
        cat "$LOG" 2>/dev/null
    fi
}

# 1. A colon is an ordinary character here; it must not open a command line.
tnt_expect_session "plain" '
send_wait ":list"
send_enter
'
check_case "|:list$" \
    "a colon types instead of opening COMMAND" \
    "the colon was treated as a command prefix"

# 2. Esc must not change modes: the keys after it still type.
tnt_expect_session "plain" '
send_lone_escape
send_wait "after-escape"
send_enter
'
check_case "|after-escape$" \
    "Esc does not leave the input" \
    "Esc changed modes, so the following keys were lost"

# 3. A leading slash runs a command.
tnt_expect_session "plain" '
send_wait "/me waves"
send_enter
'
check_case "|\*|plain waves$" \
    "a leading slash runs the command" \
    "/me was not dispatched as a command"

# 4. A doubled slash sends a literal one, so ordinary text starting with "/"
#    is still reachable.
tnt_expect_session "plain" '
send_wait "//slash"
send_enter
'
check_case "|/slash$" \
    "a doubled slash sends a literal slash" \
    "// did not produce a literal slash"

# 5. An unknown slash word is ordinary text, so a path still sends.
tnt_expect_session "plain" '
send_wait "/usr/local/bin is where it lives"
send_enter
'
check_case "|/usr/local/bin is where it lives$" \
    "an unknown slash word sends as text" \
    "a path starting with / was swallowed"

tnt_check_server_alive "server survived the default keymap session"

tnt_summary
