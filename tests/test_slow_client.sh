#!/bin/sh
# Slow interactive-client regression test for TNT.
# Usage: ./test_slow_client.sh [hold_seconds] [burst_chars]

PORT=${PORT:-2222}
HOLD_SECONDS=${1:-8}
BURST_CHARS=${2:-1600}
BIN="../tnt"
PASS=0
FAIL=0
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-slow-client-test.XXXXXX")
SERVER_PID=""
SLOW_PID=""

cleanup() {
    if [ -n "$SLOW_PID" ]; then
        kill "$SLOW_PID" 2>/dev/null || true
        wait "$SLOW_PID" 2>/dev/null || true
    fi
    exec 3>&- 2>/dev/null || true
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$STATE_DIR"
}

trap cleanup EXIT

case "$HOLD_SECONDS" in
    ''|*[!0-9]*)
        echo "Error: hold_seconds must be a positive integer"
        exit 2
        ;;
esac

case "$BURST_CHARS" in
    ''|*[!0-9]*)
        echo "Error: burst_chars must be a positive integer"
        exit 2
        ;;
esac

if [ "$HOLD_SECONDS" -lt 1 ] || [ "$BURST_CHARS" -lt 1 ]; then
    echo "Error: hold_seconds and burst_chars must be positive"
    exit 2
fi

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

SSH_EXEC_OPTS="-n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 -p $PORT"
SSH_TTY_OPTS="-e none -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5 -p $PORT"

run_ssh_timeout() {
    seconds=$1
    outfile=$2
    shift 2

    ssh $SSH_EXEC_OPTS "$@" >"$outfile" 2>&1 &
    cmd_pid=$!
    elapsed=0

    while [ "$elapsed" -lt "$seconds" ]; do
        if ! kill -0 "$cmd_pid" 2>/dev/null; then
            wait "$cmd_pid"
            return $?
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done

    if kill -0 "$cmd_pid" 2>/dev/null; then
        kill "$cmd_pid" 2>/dev/null || true
        wait "$cmd_pid" 2>/dev/null || true
    fi
    return 124
}

wait_for_health() {
    out=""
    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
        if [ -n "$SERVER_PID" ] && ! kill -0 "$SERVER_PID" 2>/dev/null; then
            return 1
        fi
        out=$(ssh $SSH_EXEC_OPTS localhost health 2>/dev/null || true)
        [ "$out" = "ok" ] && return 0
        sleep 1
    done
    return 1
}

wait_for_slow_user() {
    out=""
    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
        if [ -n "$SERVER_PID" ] && ! kill -0 "$SERVER_PID" 2>/dev/null; then
            return 1
        fi
        out=$(ssh $SSH_EXEC_OPTS localhost users --json 2>/dev/null || true)
        printf '%s\n' "$out" | grep -q '"slow"' && return 0
        sleep 1
    done
    return 1
}

echo "=== TNT Slow Client Test ==="
echo "hold=${HOLD_SECONDS}s burst_chars=$BURST_CHARS port=$PORT"

TNT_LANG=en "$BIN" \
    --bind 127.0.0.1 \
    --public-host slow.local \
    --max-connections 32 \
    --max-conn-per-ip 32 \
    --max-conn-rate-per-ip 64 \
    --rate-limit 0 \
    --idle-timeout 0 \
    --ssh-log-level 1 \
    -p "$PORT" \
    -d "$STATE_DIR" >"$STATE_DIR/server.log" 2>&1 &
SERVER_PID=$!

if wait_for_health; then
    echo "✓ server started"
    PASS=$((PASS + 1))
else
    echo "✗ server failed to start"
    sed -n '1,160p' "$STATE_DIR/server.log"
    exit 1
fi

SLOW_FIFO="$STATE_DIR/slow.out"
mkfifo "$SLOW_FIFO"
exec 3<>"$SLOW_FIFO"

(
    printf 'slow\n'
    sleep 2
    i=0
    while [ "$i" -lt "$BURST_CHARS" ]; do
        printf 'x'
        i=$((i + 1))
    done
    sleep "$HOLD_SECONDS"
) | ssh $SSH_TTY_OPTS slow@127.0.0.1 >"$SLOW_FIFO" 2>"$STATE_DIR/slow.err" &
SLOW_PID=$!

if wait_for_slow_user; then
    echo "✓ deliberately unread interactive client reached chat"
    PASS=$((PASS + 1))
else
    echo "✗ slow client did not reach chat"
    sed -n '1,120p' "$STATE_DIR/slow.err"
    FAIL=$((FAIL + 1))
fi

sleep 3

if run_ssh_timeout 5 "$STATE_DIR/health.out" localhost health &&
   grep -qx 'ok' "$STATE_DIR/health.out"; then
    echo "✓ health stayed responsive while slow client was pressured"
    PASS=$((PASS + 1))
else
    echo "✗ health blocked or returned unexpected output"
    cat "$STATE_DIR/health.out" 2>/dev/null || true
    FAIL=$((FAIL + 1))
fi

if run_ssh_timeout 5 "$STATE_DIR/stats.out" localhost stats --json &&
   grep -q '"status":"ok"' "$STATE_DIR/stats.out"; then
    echo "✓ stats stayed responsive while slow client was pressured"
    PASS=$((PASS + 1))
else
    echo "✗ stats blocked or returned unexpected output"
    cat "$STATE_DIR/stats.out" 2>/dev/null || true
    FAIL=$((FAIL + 1))
fi

FLOOD_FAIL=0
i=1
while [ "$i" -le 8 ]; do
    msg=$(printf 'slow-client responsive post %02d %0900d' "$i" 0)
    if ! run_ssh_timeout 5 "$STATE_DIR/post-$i.out" probe@localhost post "$msg" ||
       ! grep -qx 'posted' "$STATE_DIR/post-$i.out"; then
        echo "✗ post blocked or failed during slow-client pressure at $i/8"
        cat "$STATE_DIR/post-$i.out" 2>/dev/null || true
        FAIL=$((FAIL + 1))
        FLOOD_FAIL=1
        break
    fi
    i=$((i + 1))
done

if [ "$FLOOD_FAIL" -eq 0 ]; then
    echo "✓ post path stayed responsive during slow-client pressure"
    PASS=$((PASS + 1))
fi

if run_ssh_timeout 5 "$STATE_DIR/tail.out" localhost "tail -n 5" &&
   grep -q 'slow-client responsive post 08' "$STATE_DIR/tail.out"; then
    echo "✓ tail sees messages posted during slow-client pressure"
    PASS=$((PASS + 1))
else
    echo "✗ tail missing slow-client pressure messages"
    cat "$STATE_DIR/tail.out" 2>/dev/null || true
    FAIL=$((FAIL + 1))
fi

if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "✓ server survived slow-client pressure"
    PASS=$((PASS + 1))
else
    echo "✗ server exited during slow-client pressure"
    sed -n '1,160p' "$STATE_DIR/server.log"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
