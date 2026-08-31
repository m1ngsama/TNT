#!/bin/sh
# Absolute login-deadline regressions for raw SSH and username slowloris peers.

PORT=${PORT:-12369}
BIN=${BIN:-../tnt}
PASS=0
FAIL=0
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-login-timeout-test.XXXXXX")
SERVER_PID=""
RAW_PID=""
IDLE_PID=""
DRIP_PID=""

stop_process() {
    pid=$1
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
    fi
    if [ -n "$pid" ]; then
        wait "$pid" 2>/dev/null || true
    fi
}

cleanup() {
    stop_process "$RAW_PID"
    stop_process "$IDLE_PID"
    stop_process "$DRIP_PID"
    stop_process "$SERVER_PID"
    rm -rf "$STATE_DIR"
}
trap cleanup EXIT

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 not installed; skipping login timeout test"
    exit 0
fi
if ! command -v expect >/dev/null 2>&1; then
    echo "expect not installed; skipping login timeout test"
    exit 0
fi

ssh_exec() {
    ssh -n -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=3 \
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
    attempts=${2:-10}
    while [ "$attempts" -gt 0 ]; do
        [ -f "$path" ] && return 0
        sleep 1
        attempts=$((attempts - 1))
    done
    return 1
}

pass() {
    echo "✓ $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "✗ $1"
    FAIL=$((FAIL + 1))
}

echo "=== TNT Absolute Login Timeout Tests ==="

TNT_LANG=en TNT_RATE_LIMIT=0 TNT_MAX_CONN_PER_IP=64 \
    TNT_MAX_CONNECTIONS=64 "$BIN" --bind 127.0.0.1 \
    -p "$PORT" -d "$STATE_DIR" >"$STATE_DIR/server.log" 2>&1 &
SERVER_PID=$!

if wait_for_health; then
    pass "server started"
else
    fail "server failed to start"
    sed -n '1,160p' "$STATE_DIR/server.log"
    exit 1
fi

python3 - "$PORT" "$STATE_DIR/raw.ready" "$STATE_DIR/raw.elapsed" <<'PY' \
    >"$STATE_DIR/raw.log" 2>&1 &
import socket
import sys
import time

port = int(sys.argv[1])
ready_path = sys.argv[2]
elapsed_path = sys.argv[3]
started = time.monotonic()
data = bytearray()

with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
    open(ready_path, "w", encoding="utf-8").close()
    sock.settimeout(15)
    while True:
        try:
            chunk = sock.recv(4096)
        except ConnectionResetError:
            break
        if not chunk:
            break
        data.extend(chunk)

elapsed = time.monotonic() - started
if b"SSH-" not in data or elapsed < 8 or elapsed > 15:
    raise SystemExit(
        f"unexpected raw handshake lifetime={elapsed:.3f}s banner={bytes(data)!r}"
    )
with open(elapsed_path, "w", encoding="utf-8") as output:
    output.write(f"{elapsed:.3f}\n")
PY
RAW_PID=$!

cat >"$STATE_DIR/idle.expect" <<'EOF'
set port [lindex $argv 0]
set ready_path [lindex $argv 1]
set elapsed_path [lindex $argv 2]
set timeout 10
set started [clock milliseconds]
spawn ssh -e none -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=3 -p $port idle-login@127.0.0.1
expect "Enter display name"
exec touch $ready_path
set timeout 70
expect eof
set elapsed [expr {[clock milliseconds] - $started}]
if {$elapsed < 50000 || $elapsed > 70000} {
    puts stderr "unexpected idle username lifetime=${elapsed}ms"
    exit 1
}
set output [open $elapsed_path w]
puts $output $elapsed
close $output
EOF
expect "$STATE_DIR/idle.expect" "$PORT" "$STATE_DIR/idle.ready" \
    "$STATE_DIR/idle.elapsed" >"$STATE_DIR/idle.log" 2>&1 &
IDLE_PID=$!

cat >"$STATE_DIR/drip.expect" <<'EOF'
set port [lindex $argv 0]
set ready_path [lindex $argv 1]
set elapsed_path [lindex $argv 2]
set timeout 10
set started [clock milliseconds]
spawn ssh -e none -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=3 -p $port drip-login@127.0.0.1
expect "Enter display name"
exec touch $ready_path
set closed 0
set timeout 1
while {[clock milliseconds] - $started < 70000} {
    expect {
        eof {
            set closed 1
            break
        }
        timeout {
            catch {send -- "x"}
        }
    }
}
set elapsed [expr {[clock milliseconds] - $started}]
if {!$closed || $elapsed < 50000 || $elapsed > 70000} {
    puts stderr "unexpected drip username lifetime=${elapsed}ms closed=$closed"
    exit 1
}
set output [open $elapsed_path w]
puts $output $elapsed
close $output
EOF
expect "$STATE_DIR/drip.expect" "$PORT" "$STATE_DIR/drip.ready" \
    "$STATE_DIR/drip.elapsed" >"$STATE_DIR/drip.log" 2>&1 &
DRIP_PID=$!

if ! wait_for_file "$STATE_DIR/raw.ready" 10 ||
   ! wait_for_file "$STATE_DIR/idle.ready" 10 ||
   ! wait_for_file "$STATE_DIR/drip.ready" 10; then
    fail "timeout probes did not reach their intended stages"
    sed -n '1,120p' "$STATE_DIR/raw.log" 2>/dev/null || true
    sed -n '1,120p' "$STATE_DIR/idle.log" 2>/dev/null || true
    sed -n '1,120p' "$STATE_DIR/drip.log" 2>/dev/null || true
    exit 1
fi

holding=0
for _ in 1 2 3 4 5; do
    stats=$(ssh_exec localhost stats --json 2>/dev/null || true)
    if printf '%s\n' "$stats" | grep -q '"online_users":0' &&
       printf '%s\n' "$stats" | grep -q '"active_connections":4'; then
        holding=1
        break
    fi
    sleep 1
done
if [ "$holding" -eq 1 ]; then
    pass "three incomplete logins consume bounded pre-room slots"
else
    fail "incomplete login accounting was unexpected: $stats"
fi

raw_status=0
wait "$RAW_PID" || raw_status=$?
RAW_PID=""
if [ "$raw_status" -eq 0 ] &&
   grep -q 'Key exchange timed out from 127.0.0.1' "$STATE_DIR/server.log"; then
    pass "raw SSH handshake is closed at the absolute setup deadline"
else
    fail "raw SSH handshake did not close on schedule"
    sed -n '1,120p' "$STATE_DIR/raw.log" 2>/dev/null || true
fi

idle_status=0
drip_status=0
wait "$IDLE_PID" || idle_status=$?
IDLE_PID=""
wait "$DRIP_PID" || drip_status=$?
DRIP_PID=""

if [ "$idle_status" -eq 0 ]; then
    pass "silent username prompt closes after one absolute minute"
else
    fail "silent username prompt did not close on schedule"
    sed -n '1,160p' "$STATE_DIR/idle.log" 2>/dev/null || true
fi
if [ "$drip_status" -eq 0 ]; then
    pass "username byte drip cannot extend the absolute deadline"
else
    fail "username byte drip extended or broke the deadline"
    sed -n '1,160p' "$STATE_DIR/drip.log" 2>/dev/null || true
fi

settled=0
for _ in 1 2 3 4 5; do
    stats=$(ssh_exec localhost stats --json 2>/dev/null || true)
    if printf '%s\n' "$stats" | grep -q '"online_users":0' &&
       printf '%s\n' "$stats" | grep -q '"active_connections":1'; then
        settled=1
        break
    fi
    sleep 1
done
if [ "$settled" -eq 1 ] && [ "$(ssh_exec localhost health 2>/dev/null)" = ok ]; then
    pass "expired logins release counters and leave the server healthy"
else
    fail "expired logins leaked accounting or harmed server health: $stats"
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
