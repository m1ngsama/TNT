#!/bin/sh
# Module runtime regression tests for TNT.

PORT=${PORT:-12352}
PASS=0
FAIL=0
BIN="../tnt"
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-module-test.XXXXXX")
MODULE_DIR="$STATE_DIR/echo-module"
FLOOD_MODULE_DIR="$STATE_DIR/flood-module"
INVALID_MODULE_DIR="$STATE_DIR/invalid-module"
SERVER_PID=""

stop_server() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    fi
}

cleanup() {
    stop_server
    rm -rf "$STATE_DIR"
}

trap cleanup EXIT

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

SSH_OPTS="-n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -o ConnectionAttempts=3 -o ConnectTimeout=15 -p $PORT"

mkdir -p "$MODULE_DIR"
cat >"$MODULE_DIR/tnt-module.json" <<'JSON'
{
  "protocol": "tnt.module.v1",
  "name": "echo-module",
  "version": "0.1.0",
  "entrypoint": "./echo-module.sh",
  "permissions": ["message:read", "message:create"],
  "events": ["message.created"]
}
JSON

cat >"$MODULE_DIR/echo-module.sh" <<'SH'
#!/bin/sh
json_escape() {
  printf '%s' "$1" | awk '
    BEGIN { ORS = "" }
    { gsub(/\\/,"\\\\"); gsub(/"/,"\\\""); print }
  '
}
extract_string() {
  key=$1
  line=$2
  printf '%s\n' "$line" | sed -n "s/.*\"$key\"[[:space:]]*:[[:space:]]*\"\\([^\"]*\\)\".*/\\1/p"
}
while IFS= read -r line; do
  protocol=$(extract_string protocol "$line")
  plain_text=$(extract_string plain_text "$line")
  if printf '%s\n' "$line" | grep -q '"type"[[:space:]]*:[[:space:]]*"handshake"'; then
    if [ "$protocol" = "tnt.module.v1" ]; then
      printf '{"type":"handshake.ok","protocol":"tnt.module.v1","module":{"name":"echo-module","version":"0.1.0"}}\n'
    else
      printf '{"type":"error","code":"unsupported_protocol","message":"requires tnt.module.v1"}\n'
    fi
  elif printf '%s\n' "$line" | grep -q '"type"[[:space:]]*:[[:space:]]*"message.created"' && [ -n "$plain_text" ]; then
    escaped=$(json_escape "echo: $plain_text")
    printf '{"type":"message.create","plain_text":"%s"}\n' "$escaped"
  else
    printf '{"type":"event.ok"}\n'
  fi
done
SH
chmod +x "$MODULE_DIR/echo-module.sh"

mkdir -p "$FLOOD_MODULE_DIR"
cat >"$FLOOD_MODULE_DIR/tnt-module.json" <<'JSON'
{
  "protocol": "tnt.module.v1",
  "name": "flood-module",
  "version": "0.1.0",
  "entrypoint": "./flood-module.sh",
  "permissions": ["message:read", "message:create"],
  "events": ["message.created"]
}
JSON

cat >"$FLOOD_MODULE_DIR/flood-module.sh" <<'SH'
#!/bin/sh
extract_string() {
  key=$1
  line=$2
  printf '%s\n' "$line" | sed -n "s/.*\"$key\"[[:space:]]*:[[:space:]]*\"\\([^\"]*\\)\".*/\\1/p"
}
while IFS= read -r line; do
  plain_text=$(extract_string plain_text "$line")
  if printf '%s\n' "$line" | grep -q '"type"[[:space:]]*:[[:space:]]*"handshake"'; then
    printf '{"type":"handshake.ok","protocol":"tnt.module.v1","module":{"name":"flood-module","version":"0.1.0"}}\n'
  elif printf '%s\n' "$line" | grep -q '"type"[[:space:]]*:[[:space:]]*"message.created"'; then
    i=1
    while [ "$i" -le 9 ]; do
      printf '{"type":"message.create","plain_text":"flood %s: %s"}\n' "$i" "$plain_text"
      i=$((i + 1))
    done
  else
    printf '{"type":"event.ok"}\n'
  fi
done
SH
chmod +x "$FLOOD_MODULE_DIR/flood-module.sh"

mkdir -p "$INVALID_MODULE_DIR"
cat >"$INVALID_MODULE_DIR/tnt-module.json" <<'JSON'
{
  "protocol": "tnt.module.v1",
  "name": "invalid-module",
  "version": "0.1.0",
  "entrypoint": "./invalid-module.sh",
  "permissions": ["message:read", "message:create"],
  "events": ["message.created"]
}
JSON

cat >"$INVALID_MODULE_DIR/invalid-module.sh" <<'SH'
#!/bin/sh
while IFS= read -r line; do
  if printf '%s\n' "$line" | grep -q '"type"[[:space:]]*:[[:space:]]*"handshake"'; then
    printf '{"type":"handshake.ok","protocol":"tnt.module.v1","module":{"name":"invalid-module","version":"0.1.0"}}\n'
  elif printf '%s\n' "$line" | grep -q '"type"[[:space:]]*:[[:space:]]*"message.created"'; then
    printf '{"type":"not.allowed"}\n'
  else
    printf '{"type":"event.ok"}\n'
  fi
done
SH
chmod +x "$INVALID_MODULE_DIR/invalid-module.sh"

echo "=== TNT Module Runtime Tests ==="

TNT_LANG=en TNT_RATE_LIMIT=0 TNT_MODULE_PATHS="$MODULE_DIR" \
    TNT_MAX_CONN_PER_IP=256 TNT_MAX_CONNECTIONS=256 "$BIN" -p "$PORT" -d "$STATE_DIR" >"$STATE_DIR/server.log" 2>&1 &
SERVER_PID=$!

HEALTH_OUTPUT=""
for _ in 1 2 3 4 5 6 7 8 9 10; do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "x server failed to start"
        sed -n '1,160p' "$STATE_DIR/server.log"
        exit 1
    fi
    HEALTH_OUTPUT=$(ssh $SSH_OPTS localhost health 2>/dev/null || true)
    [ "$HEALTH_OUTPUT" = "ok" ] && break
    sleep 1
done

if [ "$HEALTH_OUTPUT" = "ok" ]; then
    echo "✓ server starts with module runtime"
    PASS=$((PASS + 1))
else
    echo "x health failed: $HEALTH_OUTPUT"
    sed -n '1,160p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

POST_OUTPUT=$(ssh $SSH_OPTS alice@localhost post "hello module" 2>/dev/null || true)
if [ "$POST_OUTPUT" = "posted" ]; then
    echo "✓ post succeeds with module runtime"
    PASS=$((PASS + 1))
else
    echo "x post failed: $POST_OUTPUT"
    FAIL=$((FAIL + 1))
fi

FOUND=0
for _ in 1 2 3 4 5; do
    TAIL_OUTPUT=$(ssh $SSH_OPTS localhost "tail -n 5" 2>/dev/null || true)
    if printf '%s\n' "$TAIL_OUTPUT" | grep -q 'module:echo-module.*echo: hello module'; then
        FOUND=1
        break
    fi
    sleep 1
done

if [ "$FOUND" -eq 1 ]; then
    echo "✓ module response is persisted and visible"
    PASS=$((PASS + 1))
else
    echo "x module response missing"
    printf '%s\n' "$TAIL_OUTPUT"
    sed -n '1,200p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

stop_server

TNT_LANG=en TNT_RATE_LIMIT=0 TNT_MODULE_PATHS="$FLOOD_MODULE_DIR" \
    TNT_MAX_CONN_PER_IP=256 TNT_MAX_CONNECTIONS=256 "$BIN" -p "$PORT" -d "$STATE_DIR" >"$STATE_DIR/flood-server.log" 2>&1 &
SERVER_PID=$!

HEALTH_OUTPUT=""
for _ in 1 2 3 4 5 6 7 8 9 10; do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "x flood server failed to start"
        sed -n '1,160p' "$STATE_DIR/flood-server.log"
        exit 1
    fi
    HEALTH_OUTPUT=$(ssh $SSH_OPTS localhost health 2>/dev/null || true)
    [ "$HEALTH_OUTPUT" = "ok" ] && break
    sleep 1
done

if [ "$HEALTH_OUTPUT" = "ok" ]; then
    echo "✓ server starts with flood module"
    PASS=$((PASS + 1))
else
    echo "x flood health failed: $HEALTH_OUTPUT"
    sed -n '1,160p' "$STATE_DIR/flood-server.log"
    FAIL=$((FAIL + 1))
fi

POST_OUTPUT=$(ssh $SSH_OPTS alice@localhost post "trigger flood" 2>/dev/null || true)
if [ "$POST_OUTPUT" = "posted" ]; then
    echo "✓ flood trigger post succeeds"
    PASS=$((PASS + 1))
else
    echo "x flood trigger post failed: $POST_OUTPUT"
    FAIL=$((FAIL + 1))
fi

DISABLED=0
for _ in 1 2 3 4 5; do
    if grep -q 'too many responses' "$STATE_DIR/flood-server.log"; then
        DISABLED=1
        break
    fi
    sleep 1
done

if [ "$DISABLED" -eq 1 ]; then
    echo "✓ flood module is disabled after too many responses"
    PASS=$((PASS + 1))
else
    echo "x flood module was not disabled"
    sed -n '1,200p' "$STATE_DIR/flood-server.log"
    FAIL=$((FAIL + 1))
fi

POST_OUTPUT=$(ssh $SSH_OPTS bob@localhost post "after disable" 2>/dev/null || true)
sleep 1
TAIL_OUTPUT=$(ssh $SSH_OPTS localhost "tail -n 20" 2>/dev/null || true)
HEALTH_OUTPUT=$(ssh $SSH_OPTS localhost health 2>/dev/null || true)
if [ "$POST_OUTPUT" = "posted" ] &&
   [ "$HEALTH_OUTPUT" = "ok" ] &&
   ! printf '%s\n' "$TAIL_OUTPUT" | grep -q 'module:flood-module.*after disable'; then
    echo "✓ disabled flood module stays isolated while server remains healthy"
    PASS=$((PASS + 1))
else
    echo "x disabled flood module isolation failed"
    printf '%s\n' "$TAIL_OUTPUT"
    sed -n '1,240p' "$STATE_DIR/flood-server.log"
    FAIL=$((FAIL + 1))
fi

stop_server

TNT_LANG=en TNT_RATE_LIMIT=0 TNT_MODULE_PATHS="$INVALID_MODULE_DIR" \
    TNT_MAX_CONN_PER_IP=256 TNT_MAX_CONNECTIONS=256 "$BIN" -p "$PORT" -d "$STATE_DIR" >"$STATE_DIR/invalid-server.log" 2>&1 &
SERVER_PID=$!

HEALTH_OUTPUT=""
for _ in 1 2 3 4 5 6 7 8 9 10; do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "x invalid-response server failed to start"
        sed -n '1,160p' "$STATE_DIR/invalid-server.log"
        exit 1
    fi
    HEALTH_OUTPUT=$(ssh $SSH_OPTS localhost health 2>/dev/null || true)
    [ "$HEALTH_OUTPUT" = "ok" ] && break
    sleep 1
done

if [ "$HEALTH_OUTPUT" = "ok" ]; then
    echo "✓ server starts with invalid-response module"
    PASS=$((PASS + 1))
else
    echo "x invalid-response health failed: $HEALTH_OUTPUT"
    sed -n '1,160p' "$STATE_DIR/invalid-server.log"
    FAIL=$((FAIL + 1))
fi

INVALID_POSTS_OK=1
for message in invalid-one invalid-two invalid-three; do
    POST_OUTPUT=$(ssh $SSH_OPTS carol@localhost post "$message" 2>/dev/null || true)
    [ "$POST_OUTPUT" = "posted" ] || INVALID_POSTS_OK=0
done

if [ "$INVALID_POSTS_OK" -eq 1 ]; then
    echo "✓ invalid-response trigger posts succeed"
    PASS=$((PASS + 1))
else
    echo "x invalid-response trigger post failed"
    sed -n '1,200p' "$STATE_DIR/invalid-server.log"
    FAIL=$((FAIL + 1))
fi

DISABLED=0
for _ in 1 2 3 4 5; do
    if grep -q 'invalid-module after invalid responses' "$STATE_DIR/invalid-server.log"; then
        DISABLED=1
        break
    fi
    sleep 1
done

if [ "$DISABLED" -eq 1 ]; then
    echo "✓ invalid-response module is disabled after repeated errors"
    PASS=$((PASS + 1))
else
    echo "x invalid-response module was not disabled"
    sed -n '1,240p' "$STATE_DIR/invalid-server.log"
    FAIL=$((FAIL + 1))
fi

INVALID_COUNT=$(grep -c 'ignored invalid response from invalid-module' \
    "$STATE_DIR/invalid-server.log" || true)
POST_OUTPUT=$(ssh $SSH_OPTS dave@localhost post "after invalid disable" 2>/dev/null || true)
sleep 1
INVALID_COUNT_AFTER=$(grep -c 'ignored invalid response from invalid-module' \
    "$STATE_DIR/invalid-server.log" || true)
HEALTH_OUTPUT=$(ssh $SSH_OPTS localhost health 2>/dev/null || true)
if [ "$POST_OUTPUT" = "posted" ] &&
   [ "$HEALTH_OUTPUT" = "ok" ] &&
   [ "$INVALID_COUNT_AFTER" = "$INVALID_COUNT" ]; then
    echo "✓ disabled invalid-response module stays isolated while server remains healthy"
    PASS=$((PASS + 1))
else
    echo "x disabled invalid-response module isolation failed"
    sed -n '1,260p' "$STATE_DIR/invalid-server.log"
    FAIL=$((FAIL + 1))
fi

printf '\nPASSED: %d\nFAILED: %d\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
