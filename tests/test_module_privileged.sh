#!/bin/sh
# Granted module permissions: message.post under a user nickname and presence
# records, both gated by TNT_MODULE_GRANTS.

. ./lib.sh

PORT=${PORT:-12368}
trap tnt_cleanup EXIT

tnt_skip_without_expect "privileged module test"
tnt_require_binary
tnt_state_dir module-privileged-test

echo "=== TNT Privileged Module Tests ==="

POSTER_DIR="$STATE_DIR/poster"
ECHO_DIR="$STATE_DIR/echo"
LOG="$STATE_DIR/messages.log"
TNT_POSTER_EVENTS="$STATE_DIR/poster.events"
SSH_EXEC="ssh -n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -o LogLevel=ERROR -p $PORT"
export TNT_POSTER_EVENTS

mkdir -p "$POSTER_DIR" "$ECHO_DIR"
cat >"$POSTER_DIR/tnt-module.json" <<'JSON'
{
  "protocol": "tnt.module.v1",
  "name": "poster",
  "entrypoint": "./poster.sh",
  "permissions": ["message:read", "message:create", "message:post", "presence:read"],
  "events": ["message.created"]
}
JSON
cat >"$POSTER_DIR/poster.sh" <<'SH'
#!/bin/sh
while IFS= read -r line; do
  printf '%s\n' "$line" >>"$TNT_POSTER_EVENTS"
  case "$line" in
    *'"type":"handshake"'*)
      printf '{"type":"handshake.ok","protocol":"tnt.module.v1"}\n' ;;
    *'"type":"presence.snapshot"'*)
      printf '{"type":"message.post","sender":"system","plain_text":"forged notice"}\n'
      printf '{"type":"message.post","sender":"webuser","plain_text":"hello from the web"}\n' ;;
    *'"type":"message.created"'*)
      printf '{"type":"event.ok"}\n' ;;
  esac
done
SH
chmod +x "$POSTER_DIR/poster.sh"

cat >"$ECHO_DIR/tnt-module.json" <<'JSON'
{
  "protocol": "tnt.module.v1",
  "name": "echo",
  "entrypoint": "./echo.sh",
  "permissions": ["message:read", "message:create"],
  "events": ["message.created"]
}
JSON
cat >"$ECHO_DIR/echo.sh" <<'SH'
#!/bin/sh
while IFS= read -r line; do
  case "$line" in
    *'"type":"handshake"'*)
      printf '{"type":"handshake.ok","protocol":"tnt.module.v1"}\n' ;;
    *'"plain_text":"hello from the web"'*)
      printf '{"type":"message.create","plain_text":"echo: hello from the web"}\n{"type":"event.ok"}\n' ;;
    *)
      printf '{"type":"event.ok"}\n' ;;
  esac
done
SH
chmod +x "$ECHO_DIR/echo.sh"

TNT_MODULE_PATHS="$POSTER_DIR:$ECHO_DIR"
export TNT_MODULE_PATHS

tnt_start_server "$PORT" "$STATE_DIR"
if grep -q "poster requests message:post or presence:read" "$STATE_DIR/server.log" &&
   ! grep -q "enabled poster" "$STATE_DIR/server.log"; then
    pass "an ungranted module requesting message:post is refused"
else
    fail "an ungranted module was enabled"
    sed -n '1,80p' "$STATE_DIR/server.log"
fi
kill "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null
SERVER_PID=""
rm -f "$LOG" "$TNT_POSTER_EVENTS"

TNT_MODULE_GRANTS=poster
export TNT_MODULE_GRANTS
tnt_start_server "$PORT" "$STATE_DIR"

if tnt_wait_for_fixed "$LOG" "|webuser|hello from the web"; then
    pass "an unsolicited message.post is persisted under its sender"
else
    fail "the posted message is missing"
    cat "$LOG" 2>/dev/null
    sed -n '1,80p' "$STATE_DIR/server.log"
fi

if ! grep -q "forged notice" "$LOG" &&
   grep -q "ignored invalid response from poster" "$STATE_DIR/server.log"; then
    pass "a reserved sender is a protocol failure"
else
    fail "a reserved sender was accepted"
    cat "$LOG" 2>/dev/null
fi

if tnt_wait_for_fixed "$LOG" "|module:echo|echo: hello from the web"; then
    pass "other modules receive the posted message"
else
    fail "the posted message never reached the echo module"
    sed -n '1,80p' "$STATE_DIR/server.log"
fi

if ! grep -q '"type":"message.created".*"sender":"webuser"' "$TNT_POSTER_EVENTS"; then
    pass "the posting module does not receive its own message"
else
    fail "the posting module received its own message"
fi

if sed -n '2p' "$TNT_POSTER_EVENTS" |
   grep -qF '{"type":"presence.snapshot","nicknames":[]}'; then
    pass "the first event after the handshake is an empty snapshot"
else
    fail "the presence snapshot is missing"
    cat "$TNT_POSTER_EVENTS"
fi

POST_OUTPUT=$($SSH_EXEC bob@127.0.0.1 post "from exec" 2>/dev/null || true)
if [ "$POST_OUTPUT" = "posted" ] &&
   tnt_wait_for_fixed "$TNT_POSTER_EVENTS" '"plain_text":"from exec"'; then
    pass "exec posts still reach a granted module"
else
    fail "exec post did not reach the granted module: $POST_OUTPUT"
fi

tnt_expect_session "carol" '
send -- "/nick dave\r"
expect "Nickname changed: carol"
'

if tnt_wait_for_fixed "$TNT_POSTER_EVENTS" '"type":"presence.left","nickname":"dave"'; then
    PRESENCE=$(sed -En 's/.*"type":"presence\.(joined|left)","nickname":"([^"]*)".*/\1:\2/p' \
        "$TNT_POSTER_EVENTS" | tr '\n' ' ')
    if [ "$PRESENCE" = "joined:carol left:carol joined:dave left:dave " ]; then
        pass "joins, nickname changes and leaves produce presence records"
    else
        fail "unexpected presence sequence: $PRESENCE"
    fi
else
    fail "presence records are missing"
    tnt_dump_session carol
    cat "$TNT_POSTER_EVENTS"
fi

if ! grep -q "disabling poster" "$STATE_DIR/server.log"; then
    pass "the granted module stays enabled"
else
    fail "the granted module was disabled"
    sed -n '1,80p' "$STATE_DIR/server.log"
fi

tnt_check_server_alive "server survived the privileged module session"
tnt_summary
