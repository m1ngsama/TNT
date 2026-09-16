#!/bin/sh
# Regression test: long messages wrap across rows instead of being truncated.

. ./lib.sh

PORT=${PORT:-12351}
trap tnt_cleanup EXIT

tnt_skip_without_expect "wide text view test"
tnt_require_binary
tnt_state_dir wide-text-test

SSH_OPTS=$(tnt_ssh_opts "$PORT")

echo "=== TNT Wide Text View Test ==="

# The seeded line is far wider than any terminal the test may get, so the
# assertions below do not depend on the negotiated terminal width.  TAILMARK
# only reaches the screen if the message wrapped; a truncating renderer drops
# it.
seed_ts=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
printf '%s|fixture|%s\n' "$seed_ts" \
    "😌 aaaaaaaaaa bbbbbbbbbb cccccccccc dddddddddd eeeeeeeeee ffffffffff gggggggggg hhhhhhhhhh iiiiiiiiii jjjjjjjjjj TAILMARK" \
    >>"$STATE_DIR/messages.log"
printf '%s|fixture|%s\n' "$seed_ts" "HEADMARK short line" \
    >>"$STATE_DIR/messages.log"

tnt_start_server "$PORT" "$STATE_DIR"

# The marks are asserted against the transcript below; waiting for the tail
# mark here is what proves the screen finished arriving.
VIEW_SCRIPT="$STATE_DIR/wide-text-view.expect"
cat >"$VIEW_SCRIPT" <<EOF
set timeout 10
spawn ssh $SSH_OPTS anonymous@127.0.0.1
expect "): "
send -- "viewer\r"
expect "TAILMARK"
expect "/help"
close
EOF

VIEW_OUTPUT=$(expect -f "$VIEW_SCRIPT" 2>&1)

if printf '%s' "$VIEW_OUTPUT" | grep -q "TAILMARK"; then
    pass "the tail of a long message survives on a wrapped row"
else
    fail "long message tail is missing — it was truncated, not wrapped"
    printf '%s\n' "$VIEW_OUTPUT" | tail -20
fi

if printf '%s' "$VIEW_OUTPUT" | grep -q "HEADMARK"; then
    pass "neighbouring short messages still render"
else
    fail "short message disappeared"
fi

tnt_check_server_alive "server survived wide text rendering"

tnt_summary
