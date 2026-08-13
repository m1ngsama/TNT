#!/bin/sh
# SIGINT/SIGTERM graceful-shutdown regressions: the listener stops, active
# and pre-auth sessions are reclaimed, and module children are reaped.

PORT=${PORT:-12361}
BIN=${BIN:-../tnt}
PASS=0
FAIL=0
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-shutdown-test.XXXXXX")
SERVER_PID=""
ACTIVE_PID=""
ACTIVE_INPUT_PID=""
PREAUTH_PID=""
CHURN_PID=""

stop_process() {
    pid=$1
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
    fi
    if [ -n "$pid" ]; then
        wait "$pid" 2>/dev/null || true
    fi
}

# Invoked through the EXIT trap below.
# shellcheck disable=SC2329
cleanup() {
    stop_process "$PREAUTH_PID"
    stop_process "$ACTIVE_INPUT_PID"
    stop_process "$ACTIVE_PID"
    stop_process "$CHURN_PID"
    stop_process "$SERVER_PID"
    rm -rf "$STATE_DIR"
}
trap cleanup EXIT

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 not installed; skipping graceful shutdown test"
    exit 0
fi

ssh_exec() {
    ssh -n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o BatchMode=yes -o ConnectionAttempts=2 -o ConnectTimeout=3 \
        -p "$PORT" "$@"
}

wait_for_health() {
    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
        if [ -n "$SERVER_PID" ] && ! kill -0 "$SERVER_PID" 2>/dev/null; then
            return 1
        fi
        out=$(ssh_exec localhost health 2>/dev/null || true)
        [ "$out" = "ok" ] && return 0
        sleep 1
    done
    return 1
}

wait_for_file() {
    path=$1
    for _ in 1 2 3 4 5; do
        [ -f "$path" ] && return 0
        sleep 1
    done
    return 1
}

wait_for_exit() {
    pid=$1
    for _ in 1 2 3 4 5; do
        ! kill -0 "$pid" 2>/dev/null && return 0
        sleep 1
    done
    return 1
}

start_server() {
    log=$1
    shift
    TNT_LANG=en TNT_RATE_LIMIT=0 TNT_MAX_CONN_PER_IP=64 \
        TNT_MAX_CONNECTIONS=64 "$@" "$BIN" --bind 127.0.0.1 \
        -p "$PORT" -d "$STATE_DIR" >"$log" 2>&1 &
    SERVER_PID=$!
}

echo "=== TNT Graceful Shutdown Tests ==="

# A normal interactive :quit must close the SSH channel with status 0.  This
# guards against accidentally applying the hard shutdown sweep to every
# ordinary client cleanup.
start_server "$STATE_DIR/normal-quit.log" env
if wait_for_health; then
    quit_status=0
    eof_status=0
    python3 -c 'import sys,time; sys.stdout.write("normal-user"+chr(13)); sys.stdout.flush(); time.sleep(1); sys.stdout.write(chr(27)+":quit"+chr(13)); sys.stdout.flush(); time.sleep(1)' | \
        ssh -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
            -o BatchMode=yes -o ConnectTimeout=3 -p "$PORT" localhost \
            >"$STATE_DIR/normal-quit.out" 2>/dev/null || quit_status=$?
    printf 'eof-user\r' | \
        ssh -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
            -o BatchMode=yes -o ConnectTimeout=3 -p "$PORT" localhost \
            >"$STATE_DIR/normal-eof.out" 2>/dev/null || eof_status=$?
    if [ "$quit_status" -eq 0 ] && [ "$eof_status" -eq 0 ]; then
        echo "✓ normal :quit and stdin EOF return SSH status 0"
        PASS=$((PASS + 1))
    else
        echo "✗ normal exits returned :quit=$quit_status EOF=$eof_status"
        FAIL=$((FAIL + 1))
    fi
    kill -TERM "$SERVER_PID" 2>/dev/null || true
    wait_for_exit "$SERVER_PID" || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""
else
    echo "✗ normal-quit server failed to start"
    FAIL=$((FAIL + 1))
fi

# An idle listener must leave through normal control flow on SIGINT.
start_server "$STATE_DIR/idle.log" env
if wait_for_health && kill -INT "$SERVER_PID" 2>/dev/null &&
   wait_for_exit "$SERVER_PID"; then
    status=0
    wait "$SERVER_PID" || status=$?
    SERVER_PID=""
    if [ "$status" -eq 0 ] && grep -q 'Shutting down' "$STATE_DIR/idle.log"; then
        echo "✓ SIGINT cleanly stops an idle server"
        PASS=$((PASS + 1))
    else
        echo "✗ idle server returned $status without normal shutdown"
        FAIL=$((FAIL + 1))
    fi
else
    echo "✗ idle server did not stop within five seconds"
    FAIL=$((FAIL + 1))
    stop_process "$SERVER_PID"
    SERVER_PID=""
fi

# Hold one fully active SSH exec connection and one raw pre-auth socket.  Both
# waits must be interrupted before main tears down the room.
start_server "$STATE_DIR/sessions.log" env
if wait_for_health; then
    mkfifo "$STATE_DIR/active.input"
    (printf 'graceful-user\r'; sleep 30) >"$STATE_DIR/active.input" &
    ACTIVE_INPUT_PID=$!
    ssh -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o BatchMode=yes -o ConnectTimeout=3 -p "$PORT" localhost \
        <"$STATE_DIR/active.input" >"$STATE_DIR/active.out" 2>/dev/null &
    ACTIVE_PID=$!
    python3 - "$PORT" "$STATE_DIR/preauth.ready" <<'PY' &
import socket
import sys

s = socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=3)
open(sys.argv[2], "w").close()
s.settimeout(10)
while s.recv(4096):
    pass
PY
    PREAUTH_PID=$!

    active_ready=0
    for _ in 1 2 3 4 5; do
        users=$(ssh_exec localhost users --json 2>/dev/null || true)
        if printf '%s\n' "$users" | grep -q 'graceful-user'; then
            active_ready=1
            break
        fi
        sleep 1
    done

    if [ "$active_ready" -eq 1 ] && wait_for_file "$STATE_DIR/preauth.ready" &&
       kill -TERM "$SERVER_PID" 2>/dev/null; then
        # A second termination request while workers are draining must remain
        # harmless and must not strand the condition-variable wait.
        kill -INT "$SERVER_PID" 2>/dev/null || true
    fi
    if [ "$active_ready" -eq 1 ] && [ -f "$STATE_DIR/preauth.ready" ] &&
       wait_for_exit "$SERVER_PID"; then
        status=0
        wait "$SERVER_PID" || status=$?
        SERVER_PID=""
        wait "$PREAUTH_PID" 2>/dev/null || true
        PREAUTH_PID=""
        wait "$ACTIVE_PID" 2>/dev/null || true
        ACTIVE_PID=""
        stop_process "$ACTIVE_INPUT_PID"
        ACTIVE_INPUT_PID=""
        if [ "$status" -eq 0 ] &&
           ! ssh_exec localhost health >/dev/null 2>&1; then
            echo "✓ SIGTERM reclaims active and pre-auth sessions"
            PASS=$((PASS + 1))
        else
            echo "✗ session shutdown returned $status or listener remained open"
            FAIL=$((FAIL + 1))
        fi
    else
        echo "✗ session server did not stop within five seconds"
        FAIL=$((FAIL + 1))
        stop_process "$SERVER_PID"
        SERVER_PID=""
    fi
else
    echo "✗ session server failed to start"
    FAIL=$((FAIL + 1))
fi

# Rapid connect/close churn can make poll readiness stale before accept.  The
# listener is non-blocking, so termination must still win within the deadline.
start_server "$STATE_DIR/accept-churn.log" env
if wait_for_health; then
    python3 - "$PORT" <<'PY' &
import socket
import sys
import time

deadline = time.monotonic() + 4
while time.monotonic() < deadline:
    try:
        s = socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=.2)
        s.close()
    except OSError:
        pass
PY
    CHURN_PID=$!
    sleep 1
    kill -TERM "$SERVER_PID" 2>/dev/null || true
    if wait_for_exit "$SERVER_PID"; then
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
        echo "✓ stale accept readiness cannot block shutdown"
        PASS=$((PASS + 1))
    else
        echo "✗ accept churn stranded shutdown"
        FAIL=$((FAIL + 1))
    fi
    stop_process "$CHURN_PID"
else
    echo "✗ accept-churn server failed to start"
    FAIL=$((FAIL + 1))
fi

# A real module process writes its PID after handshake.  Normal main teardown
# must terminate and reap that child rather than leaving it orphaned.
MODULE_DIR="$STATE_DIR/module"
mkdir -p "$MODULE_DIR"
cat >"$MODULE_DIR/tnt-module.json" <<'JSON'
{"protocol":"tnt.module.v1","name":"shutdown-probe","entrypoint":"./probe.sh","permissions":["message:read","message:create"],"events":["message.created"]}
JSON
cat >"$MODULE_DIR/probe.sh" <<'SH'
#!/bin/sh
printf '%s\n' "$$" > module.pid
while IFS= read -r line; do
    case "$line" in
        *'"type":"handshake"'*)
            printf '%s\n' '{"type":"handshake.ok","protocol":"tnt.module.v1","module":{"name":"shutdown-probe","version":"1"}}'
            ;;
        *) printf '%s\n' '{"type":"event.ok"}' ;;
    esac
done
SH
chmod +x "$MODULE_DIR/probe.sh"

start_server "$STATE_DIR/module.log" env TNT_MODULE_PATHS="$MODULE_DIR"
if wait_for_health && wait_for_file "$MODULE_DIR/module.pid"; then
    MODULE_PID=$(sed -n '1p' "$MODULE_DIR/module.pid")
    if kill -TERM "$SERVER_PID" 2>/dev/null && wait_for_exit "$SERVER_PID"; then
        status=0
        wait "$SERVER_PID" || status=$?
        SERVER_PID=""
        if [ "$status" -eq 0 ] && ! kill -0 "$MODULE_PID" 2>/dev/null; then
            echo "✓ graceful shutdown terminates and reaps module children"
            PASS=$((PASS + 1))
        else
            echo "✗ module child survived or server returned $status"
            FAIL=$((FAIL + 1))
        fi
    else
        echo "✗ module server did not stop within five seconds"
        FAIL=$((FAIL + 1))
    fi
else
    echo "✗ module server failed to start"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
