#!/bin/sh

PORT=${PORT:-12352}
PASS=0
FAIL=0
BIN="../tnt"
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-module-test.XXXXXX")
MODULE_DIR="$STATE_DIR/echo-module"
FLOOD_MODULE_DIR="$STATE_DIR/flood-module"
INVALID_MODULE_DIR="$STATE_DIR/invalid-module"
TIMEOUT_MODULE_DIR="$STATE_DIR/timeout-module"
ISOLATION_MODULE_ROOT="$STATE_DIR/isolation-modules"
SERVER_PID=""

stop_server() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    fi
}

start_server() {
    module_paths=$1
    log_file=$2
    shift 2
    env TNT_LANG=en TNT_RATE_LIMIT=0 TNT_MAX_CONN_PER_IP=256 \
        TNT_MAX_CONNECTIONS=256 TNT_MODULE_PATHS="$module_paths" "$@" \
        "$BIN" -p "$PORT" -d "$STATE_DIR" >"$log_file" 2>&1 &
    SERVER_PID=$!
}

wait_for_health() {
    log_file=$1
    label=$2
    HEALTH_OUTPUT=""
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo "x $label failed to start"
            sed -n '1,220p' "$log_file"
            exit 1
        fi
        HEALTH_OUTPUT=$(ssh $SSH_OPTS localhost health 2>/dev/null || true)
        [ "$HEALTH_OUTPUT" = "ok" ] && return
        sleep 1
    done
    return 1
}

cleanup() {
    stop_server
    if [ -f "$TIMEOUT_MODULE_DIR/helper.pid" ]; then
        HELPER_PID=$(sed -n '1p' "$TIMEOUT_MODULE_DIR/helper.pid")
        case "$HELPER_PID" in
            ''|*[!0-9]*) ;;
            *) kill -KILL "$HELPER_PID" 2>/dev/null || true ;;
        esac
    fi
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
if [ "${TNT_ACCESS_TOKEN+x}" = x ]; then
  exit 70
fi
if [ -n "${TNT_MODULE_ENV_MARKER:-}" ]; then
  : > "$TNT_MODULE_ENV_MARKER"
fi
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
    printf '{"type":"message.create","plain_text":"%s"}\n{"type":"event.ok"}\n' "$escaped"
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

mkdir -p "$TIMEOUT_MODULE_DIR"
cat >"$TIMEOUT_MODULE_DIR/tnt-module.json" <<'JSON'
{
  "protocol": "tnt.module.v1",
  "name": "timeout-module",
  "version": "0.1.0",
  "entrypoint": "./timeout-module.sh",
  "permissions": ["message:read", "message:create"],
  "events": ["message.created"]
}
JSON

cat >"$TIMEOUT_MODULE_DIR/timeout-module.sh" <<'SH'
#!/bin/sh
sleep 30 &
helper_pid=$!
printf '%s\n' "$helper_pid" > helper.pid

finish() {
  wait "$helper_pid" 2>/dev/null || true
  exit 0
}
trap finish HUP INT TERM

while :; do
  if ! IFS= read -r line; then
    wait "$helper_pid" 2>/dev/null || true
    exit 0
  fi
  case "$line" in
    *'"type":"handshake"'*)
      printf '{"type":"handshake.ok","protocol":"tnt.module.v1","module":{"name":"timeout-module","version":"0.1.0"}}\n'
      ;;
    *'"type":"message.created"'*)
      printf '{"type":"message.create","plain_text":"deadline one"}\n'
      sleep 0.04
      printf '{"type":"message.create","plain_text":"deadline two"}\n'
      sleep 0.04
      printf '{"type":"message.create","plain_text":"deadline three"}\n'
      sleep 0.04
      printf '{"type":"event.ok"}\n'
      ;;
    *)
      printf '{"type":"event.ok"}\n'
      ;;
  esac
done
SH
chmod +x "$TIMEOUT_MODULE_DIR/timeout-module.sh"

mkdir -p "$ISOLATION_MODULE_ROOT"
ISOLATION_MODULE_PATHS=""
module_index=1
while [ "$module_index" -le 8 ]; do
    isolation_dir="$ISOLATION_MODULE_ROOT/isolation-$module_index"
    mkdir -p "$isolation_dir"
    cat >"$isolation_dir/tnt-module.json" <<JSON
{
  "protocol": "tnt.module.v1",
  "name": "isolation-$module_index",
  "version": "0.1.0",
  "entrypoint": "./isolation-module.sh",
  "permissions": ["message:read", "message:create"],
  "events": ["message.created"]
}
JSON
    cat >"$isolation_dir/isolation-module.sh" <<'SH'
#!/bin/sh
module_name=${PWD##*/}
module_index=${module_name##*-}
event_number=0

while IFS= read -r line; do
  case "$line" in
    *'"type":"handshake"'*)
      printf '{"type":"handshake.ok","protocol":"tnt.module.v1","module":{"name":"%s","version":"0.1.0"}}\n' "$module_name"
      ;;
    *'"type":"message.created"'*)
      event_number=$((event_number + 1))
      if [ "$event_number" -eq 1 ]; then
        marker="$TNT_MODULE_COORD_FILE.event-1"
        if [ "$module_index" -eq 1 ]; then
          attempts=0
          while [ ! -f "$marker" ] && [ "$attempts" -lt 200 ]; do
            sleep 0.01
            attempts=$((attempts + 1))
          done
          if [ -f "$marker" ]; then
            printf '{"type":"message.create","plain_text":"event 1 isolation-1"}\n'
          fi
        elif [ "$module_index" -eq 8 ]; then
          : > "$marker"
          printf '{"type":"message.create","plain_text":"event 1 isolation-8"}\n'
        fi
      elif [ "$event_number" -eq 2 ]; then
        marker="$TNT_MODULE_COORD_FILE.event-2"
        if [ "$module_index" -eq 1 ]; then
          : > "$marker"
          printf '{"type":"message.create","plain_text":"event 2 isolation-1"}\n'
        elif [ "$module_index" -eq 8 ]; then
          attempts=0
          while [ ! -f "$marker" ] && [ "$attempts" -lt 200 ]; do
            sleep 0.01
            attempts=$((attempts + 1))
          done
          if [ -f "$marker" ]; then
            printf '{"type":"message.create","plain_text":"event 2 isolation-8"}\n'
          fi
        fi
      fi
      printf '{"type":"event.ok"}\n'
      ;;
    *)
      printf '{"type":"event.ok"}\n'
      ;;
  esac
done
SH
    chmod +x "$isolation_dir/isolation-module.sh"
    if [ -z "$ISOLATION_MODULE_PATHS" ]; then
        ISOLATION_MODULE_PATHS=$isolation_dir
    else
        ISOLATION_MODULE_PATHS="$ISOLATION_MODULE_PATHS:$isolation_dir"
    fi
    module_index=$((module_index + 1))
done
ISOLATION_COORD_FILE="$ISOLATION_MODULE_ROOT/last-worker-ready"

echo "=== TNT Module Runtime Tests ==="

SECRET_MARKER="$STATE_DIR/module-secret-not-inherited"
start_server "$MODULE_DIR" "$STATE_DIR/secret-server.log" \
    TNT_ACCESS_TOKEN=module-runtime-secret \
    TNT_MODULE_ENV_MARKER="$SECRET_MARKER"

SECRET_STRIPPED=0
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    if [ -f "$SECRET_MARKER" ] &&
       grep -q 'module runtime: enabled echo-module' \
           "$STATE_DIR/secret-server.log"; then
        SECRET_STRIPPED=1
        break
    fi
    sleep 0.1
done

if [ "$SECRET_STRIPPED" -eq 1 ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "✓ module child does not inherit TNT_ACCESS_TOKEN"
    PASS=$((PASS + 1))
else
    echo "x module child inherited the server access token"
    sed -n '1,180p' "$STATE_DIR/secret-server.log"
    FAIL=$((FAIL + 1))
fi

stop_server

start_server "$MODULE_DIR" "$STATE_DIR/server.log"
wait_for_health "$STATE_DIR/server.log" "server"

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
    echo "✓ batched module responses are persisted and visible"
    PASS=$((PASS + 1))
else
    echo "x module response missing"
    printf '%s\n' "$TAIL_OUTPUT"
    sed -n '1,200p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

stop_server

start_server "$TIMEOUT_MODULE_DIR" "$STATE_DIR/timeout-server.log"
wait_for_health "$STATE_DIR/timeout-server.log" "timeout server"

if [ "$HEALTH_OUTPUT" = "ok" ] &&
   [ -s "$TIMEOUT_MODULE_DIR/helper.pid" ]; then
    echo "✓ server starts with timeout module and helper"
    PASS=$((PASS + 1))
else
    echo "x timeout module or helper failed to start"
    sed -n '1,200p' "$STATE_DIR/timeout-server.log"
    FAIL=$((FAIL + 1))
fi

POST_OUTPUT=$(ssh $SSH_OPTS erin@localhost post "trigger timeout" 2>/dev/null || true)
if [ "$POST_OUTPUT" = "posted" ]; then
    echo "✓ timeout trigger post succeeds"
    PASS=$((PASS + 1))
else
    echo "x timeout trigger post failed: $POST_OUTPUT"
    FAIL=$((FAIL + 1))
fi

TIMEOUT_DISABLED=0
for _ in 1 2 3 4 5; do
    if grep -q 'disabling timeout-module after response timeout' \
        "$STATE_DIR/timeout-server.log"; then
        TIMEOUT_DISABLED=1
        break
    fi
    sleep 1
done

HELPER_PID=$(sed -n '1p' "$TIMEOUT_MODULE_DIR/helper.pid")
HELPER_STOPPED=0
for _ in 1 2 3 4 5; do
    if ! kill -0 "$HELPER_PID" 2>/dev/null; then
        HELPER_STOPPED=1
        break
    fi
    sleep 1
done

if [ "$TIMEOUT_DISABLED" -eq 1 ] && [ "$HELPER_STOPPED" -eq 1 ]; then
    echo "✓ response timeout disables module and terminates its process group"
    PASS=$((PASS + 1))
else
    echo "x response timeout did not fully stop the module process group"
    sed -n '1,240p' "$STATE_DIR/timeout-server.log"
    kill -KILL "$HELPER_PID" 2>/dev/null || true
    FAIL=$((FAIL + 1))
fi
rm -f "$TIMEOUT_MODULE_DIR/helper.pid"

TIMEOUT_COUNT=$(grep -c 'disabling timeout-module after response timeout' \
    "$STATE_DIR/timeout-server.log" || true)
POST_OUTPUT=$(ssh $SSH_OPTS frank@localhost post "after timeout disable" 2>/dev/null || true)
sleep 1
TIMEOUT_COUNT_AFTER=$(grep -c 'disabling timeout-module after response timeout' \
    "$STATE_DIR/timeout-server.log" || true)
HEALTH_OUTPUT=$(ssh $SSH_OPTS localhost health 2>/dev/null || true)
if [ "$POST_OUTPUT" = "posted" ] &&
   [ "$HEALTH_OUTPUT" = "ok" ] &&
   [ "$TIMEOUT_COUNT_AFTER" = "$TIMEOUT_COUNT" ]; then
    echo "✓ timed-out module stays disabled while server remains healthy"
    PASS=$((PASS + 1))
else
    echo "x timed-out module isolation failed"
    sed -n '1,260p' "$STATE_DIR/timeout-server.log"
    FAIL=$((FAIL + 1))
fi

stop_server

rm -f "$ISOLATION_COORD_FILE.event-1" "$ISOLATION_COORD_FILE.event-2"
start_server "$ISOLATION_MODULE_PATHS" "$STATE_DIR/isolation-server.log" \
    TNT_MODULE_COORD_FILE="$ISOLATION_COORD_FILE"
wait_for_health "$STATE_DIR/isolation-server.log" "isolation server"

ISOLATION_ENABLED=$(grep -c 'module runtime: enabled isolation-' \
    "$STATE_DIR/isolation-server.log" || true)
if [ "$HEALTH_OUTPUT" = "ok" ] && [ "$ISOLATION_ENABLED" -eq 8 ]; then
    echo "✓ server starts eight isolated module workers"
    PASS=$((PASS + 1))
else
    echo "x eight-module isolation server failed to start"
    sed -n '1,240p' "$STATE_DIR/isolation-server.log"
    FAIL=$((FAIL + 1))
fi

POST_ONE=$(ssh $SSH_OPTS grace@localhost post "trigger isolation one" 2>/dev/null || true)
POST_TWO=$(ssh $SSH_OPTS grace@localhost post "trigger isolation two" 2>/dev/null || true)
if [ "$POST_ONE" = "posted" ] && [ "$POST_TWO" = "posted" ]; then
    echo "✓ bidirectional isolation trigger posts succeed"
    PASS=$((PASS + 1))
else
    echo "x isolation trigger post failed: $POST_ONE / $POST_TWO"
    FAIL=$((FAIL + 1))
fi

ISOLATED_RESPONSES=0
for _ in 1 2 3 4 5; do
    TAIL_OUTPUT=$(ssh $SSH_OPTS localhost "tail -n 50" 2>/dev/null || true)
    if printf '%s\n' "$TAIL_OUTPUT" | \
           grep -q 'module:isolation-1.*event 1 isolation-1' &&
       printf '%s\n' "$TAIL_OUTPUT" | \
           grep -q 'module:isolation-8.*event 1 isolation-8' &&
       printf '%s\n' "$TAIL_OUTPUT" | \
           grep -q 'module:isolation-1.*event 2 isolation-1' &&
       printf '%s\n' "$TAIL_OUTPUT" | \
           grep -q 'module:isolation-8.*event 2 isolation-8'; then
        ISOLATED_RESPONSES=1
        break
    fi
    sleep 1
done

# All four messages proves the workers ran concurrently: isolation-1 blocks on
# a marker only isolation-8 can write, and isolation-8 blocks on one only
# isolation-1 can write, so serialized workers would deadlock and produce
# nothing.  A disabled module produces nothing either, so their presence also
# proves both modules answered their trigger events inside the response
# deadline.
#
# A later response timeout on some *other* event says nothing about
# concurrency, and on a loaded runner the deadline
# (TNT_MODULE_RESPONSE_TIMEOUT_MS, a 100 ms compile-time constant) is easy to
# miss.  It is reported, not failed.
if [ "$ISOLATED_RESPONSES" -eq 1 ]; then
    echo "✓ module workers resolve both forward and reverse dependencies"
    PASS=$((PASS + 1))
    if grep -Eq 'disabling isolation-(1|8) after response timeout' \
           "$STATE_DIR/isolation-server.log"; then
        echo "  note: a worker hit the 100 ms response deadline on a later" \
             "event; concurrency is still proven by the four messages above"
    fi
else
    echo "x module workers were serialized"
    printf '%s\n' "$TAIL_OUTPUT"
    sed -n '1,280p' "$STATE_DIR/isolation-server.log"
    FAIL=$((FAIL + 1))
fi

stop_server

start_server "$FLOOD_MODULE_DIR" "$STATE_DIR/flood-server.log"
wait_for_health "$STATE_DIR/flood-server.log" "flood server"

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

start_server "$INVALID_MODULE_DIR" "$STATE_DIR/invalid-server.log"
wait_for_health "$STATE_DIR/invalid-server.log" "invalid-response server"

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
