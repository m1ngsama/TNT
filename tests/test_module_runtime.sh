#!/bin/sh
# Module runtime regression tests for TNT.

PORT=${PORT:-12352}
PASS=0
FAIL=0
BIN="../tnt"
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-module-test.XXXXXX")
MODULE_DIR="$STATE_DIR/echo-module"
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$STATE_DIR"
}

trap cleanup EXIT

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

SSH_OPTS="-n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -p $PORT"

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

echo "=== TNT Module Runtime Tests ==="

TNT_LANG=en TNT_RATE_LIMIT=0 TNT_MODULE_PATHS="$MODULE_DIR" \
    "$BIN" -p "$PORT" -d "$STATE_DIR" >"$STATE_DIR/server.log" 2>&1 &
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

printf '\nPASSED: %d\nFAILED: %d\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
