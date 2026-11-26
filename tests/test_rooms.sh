#!/bin/sh
# End-to-end test: :join opens a room with its own log, :rooms lists it,
# :leave returns to the lobby, and the room's history survives a restart.

. ./lib.sh

PORT=${PORT:-12366}
trap tnt_cleanup EXIT

tnt_skip_without_expect "rooms test"
tnt_require_binary
tnt_state_dir rooms-test

echo "=== TNT Rooms Test ==="

TNT_KEYMAP=default
export TNT_KEYMAP
tnt_start_server "$PORT" "$STATE_DIR"

LOBBY="$STATE_DIR/messages.log"
OPS="$STATE_DIR/room-ops.log"

tnt_expect_session "alice" '
send_wait "/join Bad"
send -- "\r"
expect "A room name is 1 to 20"
send -- "q"
send_wait "/join ops"
send -- "\r"
expect "You are in ops."
send -- "q"
expect "#ops online 1"
send_wait "deploy now"
send -- "\r"
expect ": deploy now"
send_wait "/rooms"
send -- "\r"
expect "you are here"
send -- "q"
send_wait "/leave"
send -- "\r"
expect "You are in lobby."
send -- "q"
send_wait "back in the lobby"
send -- "\r"
expect ": back in the lobby"
'

if tnt_wait_for_fixed "$OPS" '|alice|deploy now|'; then
    pass "a message sent in ops is logged to room-ops.log"
else
    fail "room-ops.log did not get the message"
    cat "$OPS" 2>/dev/null
    tnt_dump_session alice
fi

if tnt_wait_for_fixed "$LOBBY" '|alice|back in the lobby|' &&
   ! grep -qF 'deploy now' "$LOBBY"; then
    pass "the lobby keeps messages.log and does not see the room's messages"
else
    fail "messages.log is wrong after :leave"
    cat "$LOBBY" 2>/dev/null
fi

if grep -qF 'alice joined the room' "$OPS" &&
   grep -qF 'alice left the room' "$OPS"; then
    pass "joining and leaving a room posts notices there"
else
    fail "ops is missing join or leave notices"
    cat "$OPS" 2>/dev/null
fi

if grep -qF 'A room name is 1 to 20' "$STATE_DIR/alice.session.log" &&
   [ ! -e "$STATE_DIR/room-Bad.log" ]; then
    pass "an invalid room name is refused"
else
    fail "an invalid room name was not refused"
fi

kill "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null
tnt_start_server "$PORT" "$STATE_DIR"

tnt_expect_session "bob" '
send_wait "/rooms"
send -- "\r"
expect "ops"
send -- "q"
send_wait "/join ops"
send -- "\r"
expect "You are in ops."
send -- "q"
expect "deploy now"
send_wait "/last 5"
send -- "\r"
expect "alice: deploy now"
send -- "q"
'

if grep -qF 'alice: deploy now' "$STATE_DIR/bob.session.log"; then
    pass "a room and its history are read back at startup"
else
    fail "ops did not come back after a restart"
    tnt_dump_session bob
fi

tnt_summary
