#!/bin/sh
# message:private: remote users in :users and completion, private messages
# between SSH sessions and a module, and clearing remote users when the
# module is disabled.

. ./lib.sh

PORT=${PORT:-12370}
trap tnt_cleanup EXIT

tnt_skip_without_expect "private module test"
tnt_require_binary
tnt_state_dir module-private-test

echo "=== TNT Private Message Module Tests ==="

RELAY_DIR="$STATE_DIR/relay"
SECOND_DIR="$STATE_DIR/relay2"
LOG="$STATE_DIR/messages.log"
TNT_RELAY_EVENTS="$STATE_DIR/relay.events"
TNT_RELAY_CTL="$STATE_DIR/relay.ctl"
export TNT_RELAY_EVENTS TNT_RELAY_CTL

mkdir -p "$RELAY_DIR" "$SECOND_DIR"
cat >"$RELAY_DIR/tnt-module.json" <<'JSON'
{
  "protocol": "tnt.module.v1",
  "name": "relay",
  "entrypoint": "./relay.sh",
  "permissions": ["message:read", "message:create", "message:private"],
  "events": ["message.created"]
}
JSON
# Every line appended to the control file becomes a module record.
cat >"$RELAY_DIR/relay.sh" <<'SH'
#!/bin/sh
while IFS= read -r line; do
  printf '%s\n' "$line" >>"$TNT_RELAY_EVENTS"
  case "$line" in
    *'"type":"handshake"'*)
      printf '{"type":"handshake.ok","protocol":"tnt.module.v1"}\n'
      tail -n +1 -f "$TNT_RELAY_CTL" & ;;
    *'"type":"message.created"'*)
      printf '{"type":"event.ok"}\n' ;;
  esac
done
SH
chmod +x "$RELAY_DIR/relay.sh"
sed 's/"name": "relay"/"name": "relay2"/' "$RELAY_DIR/tnt-module.json" \
    >"$SECOND_DIR/tnt-module.json"
cp "$RELAY_DIR/relay.sh" "$SECOND_DIR/relay.sh"

ctl() { printf '%s\n' "$1" >>"$TNT_RELAY_CTL"; }
invalid_count() { grep -c "ignored invalid response from relay" "$STATE_DIR/server.log"; }

ctl '{"type":"presence.remote","nicknames":["webby","Zed"]}'
printf 'member\n' >"$STATE_DIR/identities"

TNT_MODULE_PATHS="$RELAY_DIR:$SECOND_DIR"
TNT_MODULE_GRANTS="relay:relay2"
export TNT_MODULE_PATHS TNT_MODULE_GRANTS
tnt_start_server "$PORT" "$STATE_DIR"

if grep -q "relay2 requests message:private, which relay already holds" \
        "$STATE_DIR/server.log" &&
   grep -q "enabled relay$" "$STATE_DIR/server.log" &&
   ! grep -q "enabled relay2" "$STATE_DIR/server.log"; then
    pass "only the first message:private module is enabled"
else
    fail "a second message:private module was enabled"
    sed -n '1,40p' "$STATE_DIR/server.log"
fi

cat >"$STATE_DIR/carol.expect" <<EOF
set timeout 15
source "$PWD/lib.exp"
proc want {pattern} {
    expect {
        -exact \$pattern {}
        timeout { puts "\nwant: no \$pattern"; exit 1 }
    }
}
spawn ssh $(tnt_ssh_opts "$PORT") anonymous@127.0.0.1
want "): "
send -- "carol\r"
want "/help"
send -- "/users\r"
want "· 3"
want "carol"
want "webby"
want "· app"
want "Zed"
send -- "q"
want \$TNT_INPUT_PROMPT
send -- "hi @we\t"
want "hi @webby "
send -- "\025"
want \$TNT_INPUT_PROMPT
send -- "/vim\r"
want "Keys: vim"
send -- "q"
want "NORMAL"
send -- ":"
send -- "msg z\t"
want "msg Zed "
send -- "hello app\r"
want "Private message sent to Zed"
send -- "q"
want "NORMAL"
send -- ":"
send -- "inbox\r"
want "Private messages"
exec sh -c {printf '%s\n' '{"type":"private.post","from":"webby","to":"CAROL","plain_text":"ping from app"}' >>"$TNT_RELAY_CTL"}
want "ping from app"
send -- "q"
want "NORMAL"
send -- ":"
send -- "reply pong\r"
want "Private message sent to webby"
send -- "q"
want "NORMAL"
send -- ":"
send -- "msg nobody hi\r"
want "User 'nobody' not found"
EOF
if expect -f "$STATE_DIR/carol.expect" >"$STATE_DIR/carol.session.log" 2>&1; then
    pass "remote users are listed, completed, and reachable with :msg and :reply"
else
    fail "remote user session"
    tnt_dump_session carol
fi

if tnt_wait_for_fixed "$TNT_RELAY_EVENTS" \
        '{"type":"private.created","from":"carol","to":"Zed","plain_text":"hello app","timestamp":"' &&
   grep -F '"plain_text":"hello app"' "$TNT_RELAY_EVENTS" |
       grep -Eq '"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z","verified":false}$'; then
    pass ":msg to a remote user sends private.created"
else
    fail "private.created for :msg is missing or malformed"
    cat "$TNT_RELAY_EVENTS"
fi

if tnt_wait_for_fixed "$TNT_RELAY_EVENTS" \
        '{"type":"private.created","from":"carol","to":"webby","plain_text":"pong"'; then
    pass ":reply to a private.post sender goes back through private.created"
else
    fail "the :reply did not reach the module"
    cat "$TNT_RELAY_EVENTS"
fi

if ! grep -qE "hello app|ping from app|pong" "$LOG" 2>/dev/null &&
   ! grep -q '"type":"message.created".*\(hello app\|ping from app\|pong\)' \
        "$TNT_RELAY_EVENTS"; then
    pass "private messages are neither logged nor published"
else
    fail "a private message leaked"
    cat "$LOG" "$TNT_RELAY_EVENTS"
fi

BEFORE=$(invalid_count)
ctl '{"type":"private.post","from":"webby","to":"nobody","plain_text":"lost"}'
ctl '{"type":"presence.remote","nicknames":["webby","bad|name"]}'
ctl '{"type":"private.post","from":"system","to":"carol","plain_text":"forged"}'
ctl '{"type":"presence.remote","nicknames":["webby","Zed"]}'
ctl '{"type":"private.post","from":"member","to":"carol","plain_text":"forged"}'
ctl '{"type":"presence.remote","nicknames":["webby","Zed","yuki"]}'
if tnt_wait_for_count "$STATE_DIR/server.log" \
        "ignored invalid response from relay" $((BEFORE + 3)) &&
   [ "$(invalid_count)" -eq $((BEFORE + 3)) ] &&
   ! grep -q "disabling relay" "$STATE_DIR/server.log"; then
    pass "invalid records and reserved senders are protocol failures; an unknown target is ignored"
else
    fail "unexpected invalid-record handling"
    sed -n '1,80p' "$STATE_DIR/server.log"
fi

if tnt_expect_session "dave" '
send -- "/users\r"
expect {
    "yuki" {}
    timeout { exit 1 }
}
'; then
    pass "a later presence.remote replaces the remote set"
else
    fail "the remote set was not replaced"
    tnt_dump_session dave
fi

ctl 'not json'
ctl 'not json'
ctl 'not json'
if tnt_wait_for "$STATE_DIR/server.log" "disabling relay after invalid responses"; then
    pass "the module is disabled after three invalid records"
else
    fail "the module was not disabled"
    sed -n '1,80p' "$STATE_DIR/server.log"
fi

if tnt_expect_session "erin" '
send -- "/users\r"
expect {
    "Online users" {}
    timeout { exit 1 }
}
expect {
    -timeout 1
    "· app" { exit 1 }
    timeout {}
}
send -- "q"
expect -exact $TNT_INPUT_PROMPT
send -- "/msg webby hi\r"
expect {
    "User '"'"'webby'"'"' not found" {}
    timeout { exit 1 }
}
'; then
    pass "a disabled module's remote users disappear"
else
    fail "remote users outlived the module"
    tnt_dump_session erin
fi

tnt_check_server_alive "server survived the private module session"
tnt_summary
