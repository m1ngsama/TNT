#!/bin/sh
# End-to-end test: a signed-in member who comes back sees a divider above the
# chat that arrived while they were away.

. ./lib.sh

PORT=${PORT:-12365}
trap tnt_cleanup EXIT

tnt_skip_without_expect "read marker test"
tnt_require_binary
tnt_state_dir read-marker-test

echo "=== TNT Read Marker Test ==="

KEY="$STATE_DIR/key"
ssh-keygen -q -t ed25519 -N "" -C "" -f "$KEY"
printf 'alice %s\n' "$(ssh-keygen -lf "$KEY.pub" | cut -d' ' -f2)" \
    >"$STATE_DIR/identities"
chmod 600 "$STATE_DIR/identities"

TNT_KEYMAP=default
export TNT_KEYMAP
tnt_start_server "$PORT" "$STATE_DIR"

LOG="$STATE_DIR/messages.log"
SSH_KEY="$(tnt_ssh_opts "$PORT") -o IdentitiesOnly=yes -i $KEY"
SSH_KEYLESS="$(tnt_ssh_opts "$PORT") -o PubkeyAuthentication=no"
EXEC="ssh -n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
-o BatchMode=yes -o LogLevel=ERROR -o PubkeyAuthentication=no -p $PORT"

# Session $1 connects with ssh options $2 as login $3; $4 runs once the room
# is on screen.  Messages carry whole-second timestamps, so a session that
# leaves waits out its second before anyone posts.
visit() {
    _before=$(grep -c 'left the room' "$LOG" 2>/dev/null)
    cat >"$STATE_DIR/$1.expect" <<EOF
set timeout 15
spawn ssh $2 $3@127.0.0.1
$4
expect "/help"
close
EOF
    expect -f "$STATE_DIR/$1.expect" >"$STATE_DIR/$1.session.log" 2>&1
    tnt_poll_until 10 sh -c "[ \$(grep -c 'left the room' '$LOG') -gt $_before ]"
    sleep 1
}

screen() {
    sed 's/\x1b\[[0-9;]*[A-Za-z]//g' "$STATE_DIR/$1.session.log"
}

MEMBER='expect "q:close"
send -- "q"'
GUEST='expect "): "
send -- "carol\r"'

visit first "$SSH_KEY" alice "$MEMBER"
$EXEC bob@localhost post 'while you were out' >/dev/null 2>&1
visit back "$SSH_KEY" alice "$MEMBER"

if screen back | grep -q 'new since you left' &&
   screen back | grep -A1 'new since you left' | grep -q 'bob: while you were out'; then
    pass "a returning member sees the divider right above the new chat"
else
    fail "no divider above the chat that arrived while away"
    tnt_dump_session back
fi

visit again "$SSH_KEY" alice "$MEMBER"
if screen again | grep -q 'new since you left'; then
    fail "a divider appeared with no chat since the member left"
else
    pass "no divider when nothing was said while away"
fi

visit guest "$SSH_KEYLESS" carol "$GUEST"
$EXEC bob@localhost post 'for carol' >/dev/null 2>&1
visit guest-back "$SSH_KEYLESS" carol "$GUEST"
if screen guest-back | grep -q 'new since you left'; then
    fail "an anonymous nickname got a divider"
else
    pass "anonymous nicknames get no divider"
fi

tnt_summary
