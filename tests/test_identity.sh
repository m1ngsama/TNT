#!/bin/sh
# Verified identities: :login through an identity:write module, reserved
# names, :logout, SSH key sign-in, verified metadata, and exec post.

. ./lib.sh

PORT=${PORT:-12369}
trap tnt_cleanup EXIT

tnt_skip_without_expect "identity test"
tnt_require_binary
tnt_state_dir identity-test

echo "=== TNT Identity Tests ==="

MODULE_DIR="$STATE_DIR/ident"
IDENTITIES="$STATE_DIR/identities"
LOG="$STATE_DIR/messages.log"
TNT_IDENT_EVENTS="$STATE_DIR/ident.events"
KEY="$STATE_DIR/key"
OTHER_KEY="$STATE_DIR/other"
export TNT_IDENT_EVENTS

ssh-keygen -q -t ed25519 -N "" -C "" -f "$KEY"
ssh-keygen -q -t ed25519 -N "" -C "" -f "$OTHER_KEY"
FINGERPRINT=$(ssh-keygen -lf "$KEY.pub" | cut -d' ' -f2)

SSH_KEYLESS="$(tnt_ssh_opts "$PORT") -o PubkeyAuthentication=no"
SSH_KEY="$(tnt_ssh_opts "$PORT") -o IdentitiesOnly=yes -i $KEY"
SSH_EXEC="ssh -n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
-o BatchMode=yes -o LogLevel=ERROR -o IdentitiesOnly=yes -p $PORT"

# Run expect body $2 as session $1.  $3 holds the ssh options and $4 the
# login name; the body starts right after the connection opens.
run_session() {
    _script="$STATE_DIR/$1.expect"
    cat >"$_script" <<EOF
set timeout 15
source "$PWD/lib.exp"
spawn ssh $3 ${4:-anonymous}@127.0.0.1
$2
close
EOF
    expect -f "$_script" >"$STATE_DIR/$1.session.log" 2>&1
}

# --- Without an identity module ------------------------------------------

tnt_start_server "$PORT" "$STATE_DIR"

if run_session nomodule '
expect "): "
send -- "bob\r"
expect "/help"
send -- "/login\r"
expect {
    "Sign-in is not available on this server." {}
    timeout { exit 1 }
}
' "$SSH_KEYLESS"; then
    pass ":login without an identity module says sign-in is unavailable"
else
    fail ":login without a module"
    tnt_dump_session nomodule
fi

if [ "$(ssh -n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o BatchMode=yes -o PubkeyAuthentication=no -o LogLevel=ERROR \
        -p "$PORT" bob@127.0.0.1 health 2>&1)" = "ok" ]; then
    pass "a keyless BatchMode client still connects"
else
    fail "a keyless BatchMode client was refused"
fi

cat >"$STATE_DIR/anon.expect" <<EOF
set timeout 15
spawn ssh $SSH_KEYLESS anonymous@127.0.0.1
set a \$spawn_id
expect -i \$a "): "
send -i \$a -- "\r"
expect -i \$a "/help"
spawn ssh $SSH_KEYLESS anonymous@127.0.0.1
set b \$spawn_id
expect -i \$b "): "
send -i \$b -- "\r"
expect {
    -i \$b "/help" {}
    -i \$b timeout { exit 1 }
}
EOF
if expect -f "$STATE_DIR/anon.expect" >"$STATE_DIR/anon.session.log" 2>&1 &&
   tnt_wait_for_fixed "$LOG" "|system|anonymous2 joined the room"; then
    pass "a second blank nickname joins as anonymous2"
else
    fail "a second blank nickname did not join"
    tnt_dump_session anon
fi

kill "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null
SERVER_PID=""

# --- With a fake identity module -----------------------------------------

mkdir -p "$MODULE_DIR"
cat >"$MODULE_DIR/tnt-module.json" <<'JSON'
{
  "protocol": "tnt.module.v1",
  "name": "ident",
  "entrypoint": "./ident.sh",
  "permissions": ["message:read", "message:create", "message:post", "identity:write"],
  "events": ["message.created"]
}
JSON
cat >"$MODULE_DIR/ident.sh" <<'SH'
#!/bin/sh
while IFS= read -r line; do
  printf '%s\n' "$line" >>"$TNT_IDENT_EVENTS"
  case "$line" in
    *'"type":"handshake"'*)
      printf '{"type":"handshake.ok","protocol":"tnt.module.v1"}\n'
      printf '{"type":"identity.reserve","handle":"webby"}\n' ;;
    *'"type":"identity.requested"'*)
      session=$(printf '%s' "$line" | sed -n 's/.*"session":"\([^"]*\)".*/\1/p')
      printf '{"type":"identity.notice","session":"%s","plain_text":"Enter code TEST-CODE"}\n' "$session"
      printf '{"type":"identity.verified","session":"%s","handle":"alice"}\n' "$session" ;;
    *'"type":"message.created"'*'"plain_text":"web hello"'*)
      printf '{"type":"event.ok"}\n' ;;
    *'"type":"message.created"'*)
      printf '{"type":"event.ok"}\n' ;;
  esac
done
SH
chmod +x "$MODULE_DIR/ident.sh"

TNT_MODULE_PATHS="$MODULE_DIR"
export TNT_MODULE_PATHS

tnt_start_server "$PORT" "$STATE_DIR"
if grep -q "ident requests message:post, presence:read or identity:write" \
        "$STATE_DIR/server.log"; then
    pass "an ungranted identity:write module is refused"
else
    fail "an ungranted identity:write module was not refused"
    sed -n '1,40p' "$STATE_DIR/server.log"
fi
kill "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null
SERVER_PID=""

SECOND_DIR="$STATE_DIR/second"
mkdir -p "$SECOND_DIR"
sed 's/"name": "ident"/"name": "second"/' "$MODULE_DIR/tnt-module.json" \
    >"$SECOND_DIR/tnt-module.json"
cp "$MODULE_DIR/ident.sh" "$SECOND_DIR/ident.sh"
TNT_MODULE_PATHS="$MODULE_DIR:$SECOND_DIR"
TNT_MODULE_GRANTS="ident:second"
export TNT_MODULE_PATHS TNT_MODULE_GRANTS
tnt_start_server "$PORT" "$STATE_DIR"

if grep -q "second requests identity:write, which ident already holds" \
        "$STATE_DIR/server.log" &&
   grep -q "enabled ident" "$STATE_DIR/server.log" &&
   ! grep -q "enabled second" "$STATE_DIR/server.log"; then
    pass "only the first identity:write module is enabled"
else
    fail "a second identity:write module was enabled"
    sed -n '1,40p' "$STATE_DIR/server.log"
fi

if tnt_wait_for "$IDENTITIES" "^webby$"; then
    pass "identity.reserve adds a handle without a key"
else
    fail "identity.reserve did not reach the identities file"
    cat "$IDENTITIES" 2>/dev/null
fi

if [ "$(ls -l "$IDENTITIES" | cut -c1-10)" = "-rw-------" ]; then
    pass "the identities file has mode 0600"
else
    fail "the identities file mode is wrong"
fi

# A holds "alice" before it is reserved; B signs in as alice and A steps aside.
cat >"$STATE_DIR/login.expect" <<EOF
set timeout 15
source "$PWD/lib.exp"
proc want {id pattern} {
    expect {
        -i \$id -exact \$pattern {}
        -i \$id timeout { puts "\nwant: no \$pattern"; exit 1 }
    }
}
spawn ssh $SSH_KEYLESS anonymous@127.0.0.1
set a \$spawn_id
want \$a "): "
send -i \$a -- "alice\r"
want \$a "/help"

spawn ssh $SSH_KEYLESS anonymous@127.0.0.1
set b \$spawn_id
want \$b "): "
send -i \$b -- "bob\r"
want \$b "/help"
send -i \$b -- "/login\r"
want \$b "TEST-CODE"
want \$b "Signed in as @alice."
want \$b "ssh alice@localhost -p $PORT"

want \$a "belongs to a signed-in member"

send -i \$b -- "q"
want \$b \$TNT_INPUT_PROMPT
send -i \$b -- "hello verified\r"
want \$b "m: hello verified"
send -i \$b -- "/nick zed\r"
want \$b "Use :logout before changing your name."
send -i \$b -- "q"
sleep 0.3
send -i \$b -- "/logout\r"
want \$b "Signed out. Your name is now"
send -i \$b -- "q"
want \$b \$TNT_INPUT_PROMPT
send -i \$b -- "after logout\r"
want \$b "m: after logout"
EOF
if expect -f "$STATE_DIR/login.expect" >"$STATE_DIR/login.session.log" 2>&1; then
    pass ":login shows the module notice and signs the session in"
else
    fail ":login flow"
    tnt_dump_session login
fi

if tnt_wait_for_fixed "$LOG" "|system|alice renamed to anonymous" &&
   tnt_wait_for_fixed "$LOG" "|system|bob renamed to alice"; then
    pass "the unverified holder of a handle is renamed when its owner signs in"
else
    fail "renames missing"
    cat "$LOG"
fi

if tnt_wait_for_fixed "$TNT_IDENT_EVENTS" '"identity.requested","session":"s-' &&
   grep -qF '"nickname":"bob"}' "$TNT_IDENT_EVENTS"; then
    pass ":login sends identity.requested with the session and nickname"
else
    fail "identity.requested is missing"
    cat "$TNT_IDENT_EVENTS"
fi

if tnt_wait_for_fixed "$TNT_IDENT_EVENTS" \
        '"sender":"alice","kind":"text","plain_text":"hello verified","metadata":{"verified":true}'; then
    pass "a verified session's messages carry metadata.verified"
else
    fail "metadata.verified is missing"
    cat "$TNT_IDENT_EVENTS"
fi

if tnt_wait_for_fixed "$TNT_IDENT_EVENTS" '"plain_text":"after logout","metadata":{}'; then
    pass ":logout drops metadata.verified"
else
    fail "a signed-out session still looks verified"
    cat "$TNT_IDENT_EVENTS"
fi

if grep -q "^alice$" "$IDENTITIES"; then
    pass "a keyless sign-in reserves the handle without a key"
else
    fail "the identities file is wrong after a keyless sign-in"
    cat "$IDENTITIES"
fi

if run_session reserved "
expect \"): \"
send -- \"Alice\r\"
expect {
    \"@Alice belongs to a signed-in member.\" {}
    timeout { exit 1 }
}
send -- \"q\"
sleep 0.3
send -- \"/nick webby\r\"
expect {
    \"@webby belongs to a signed-in member.\" {}
    timeout { exit 1 }
}
" "$SSH_KEYLESS"; then
    pass "reserved names are refused at the prompt and by :nick"
else
    fail "a reserved name was accepted"
    tnt_dump_session reserved
fi

if run_session bindkey '
expect "): "
send -- "carol\r"
expect "/help"
send -- "/login\r"
expect {
    "Signed in as @alice." {}
    timeout { exit 1 }
}
' "$SSH_KEY" alice &&
   tnt_wait_for_fixed "$IDENTITIES" "alice $FINGERPRINT"; then
    pass "signing in over a public key binds the key to the handle"
else
    fail "the key was not bound"
    tnt_dump_session bindkey
    cat "$IDENTITIES"
fi

if run_session otherlogin '
expect {
    "): " {}
    "Signed in as" { exit 1 }
    timeout { exit 1 }
}
' "$SSH_KEY"; then
    pass "a bound key is recognised only under the handle as login name"
else
    fail "a bound key was recognised under another login name"
    tnt_dump_session otherlogin
fi

POST_OUTPUT=$($SSH_EXEC -i "$KEY" alice@127.0.0.1 post "from my key" 2>&1)
POST_STATUS=$?
if [ "$POST_STATUS" -eq 0 ] && [ "$POST_OUTPUT" = "posted" ] &&
   tnt_wait_for_fixed "$TNT_IDENT_EVENTS" \
        '"plain_text":"from my key","metadata":{"verified":true}'; then
    pass "exec post under a reserved name works with its key and is verified"
else
    fail "exec post with the bound key: $POST_STATUS $POST_OUTPUT"
fi

POST_OUTPUT=$($SSH_EXEC -i "$OTHER_KEY" Alice@127.0.0.1 post "forged" 2>&1)
POST_STATUS=$?
if [ "$POST_STATUS" -eq 1 ] &&
   printf '%s' "$POST_OUTPUT" | grep -q "belongs to a signed-in member" &&
   ! grep -q "forged" "$LOG"; then
    pass "exec post under a reserved name is refused without its key"
else
    fail "exec post was not refused: $POST_STATUS $POST_OUTPUT"
fi

if run_session autologin '
expect {
    "): " { exit 1 }
    "Signed in as @alice." {}
    timeout { exit 1 }
}
send -- "q"
sleep 0.3
send -- "/logout\r"
expect {
    "Signed out. Your name is now" {}
    timeout { exit 1 }
}
' "$SSH_KEY" alice; then
    pass "a bound key signs in on reconnect without the prompt"
else
    fail "automatic sign-in by key"
    tnt_dump_session autologin
fi

if tnt_poll_until 5 sh -c "! grep -qF '$FINGERPRINT' '$IDENTITIES'" &&
   grep -q "^alice$" "$IDENTITIES"; then
    pass ":logout removes the session's key and keeps the handle reserved"
else
    fail ":logout left the key bound"
    cat "$IDENTITIES"
fi

if ! grep -q "disabling ident" "$STATE_DIR/server.log"; then
    pass "the identity module stays enabled"
else
    fail "the identity module was disabled"
    sed -n '1,80p' "$STATE_DIR/server.log"
fi

tnt_check_server_alive "server survived the identity session"
tnt_summary
