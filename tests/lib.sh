#!/bin/sh
# Shared helpers for the integration suites.  Source with `. ./lib.sh`.

TNT_BIN=${TNT_BIN:-../tnt}
PASS=0
FAIL=0
SERVER_PID=""
STATE_DIR=""

pass() { echo "✓ $1"; PASS=$((PASS + 1)); }
fail() { echo "✗ $1"; FAIL=$((FAIL + 1)); }

tnt_skip_without_expect() {
    command -v expect >/dev/null 2>&1 && return 0
    echo "expect not installed; skipping $1"
    exit 0
}

tnt_require_binary() {
    [ -f "$TNT_BIN" ] && return 0
    echo "Error: Binary $TNT_BIN not found. Run make first." >&2
    exit 1
}

tnt_state_dir() {
    STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-$1.XXXXXX")
}

tnt_ssh_opts() {
    printf '%s' "-e none -tt -o StrictHostKeyChecking=no \
-o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectionAttempts=3 \
-o ConnectTimeout=15 -p $1"
}

tnt_cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    [ -n "$STATE_DIR" ] && rm -rf "$STATE_DIR"
}

# Run a command until it succeeds or $1 seconds elapse.  Replaces a fixed
# sleep wherever the wait is for a condition the test can observe.
tnt_poll_until() {
    _timeout=$1
    shift
    _deadline=$(( $(date +%s) + _timeout ))
    while :; do
        if "$@"; then
            return 0
        fi
        [ "$(date +%s)" -lt "$_deadline" ] || return 1
        sleep 0.05
    done
}

tnt_file_has() { grep -q "$2" "$1" 2>/dev/null; }
tnt_file_has_fixed() { grep -qF "$2" "$1" 2>/dev/null; }

tnt_file_count_at_least() {
    [ "$(grep -c "$2" "$1" 2>/dev/null || echo 0)" -ge "$3" ]
}

tnt_file_exists() { [ -f "$1" ]; }
tnt_pid_gone() { ! kill -0 "$1" 2>/dev/null; }

tnt_wait_for_file() { tnt_poll_until "${2:-5}" tnt_file_exists "$1"; }
tnt_wait_for_exit() { tnt_poll_until "${2:-5}" tnt_pid_gone "$1"; }

tnt_wait_for() { tnt_poll_until "${3:-5}" tnt_file_has "$1" "$2"; }
tnt_wait_for_fixed() { tnt_poll_until "${3:-5}" tnt_file_has_fixed "$1" "$2"; }

tnt_wait_for_count() {
    tnt_poll_until "${4:-5}" tnt_file_count_at_least "$1" "$2" "$3"
}

# Start a server on $1 with state dir $2 and wait until it is listening.
# Export any TNT_* overrides before calling; the defaults below only fill in
# what the caller left unset.
tnt_start_server() {
    _port=$1
    _state=$2
    TNT_LANG=${TNT_LANG:-en} \
    TNT_RATE_LIMIT=${TNT_RATE_LIMIT:-0} \
    TNT_MAX_CONN_PER_IP=${TNT_MAX_CONN_PER_IP:-256} \
    TNT_MAX_CONNECTIONS=${TNT_MAX_CONNECTIONS:-256} \
        "$TNT_BIN" --bind 127.0.0.1 -p "$_port" -d "$_state" \
        >"$_state/server.log" 2>&1 &
    SERVER_PID=$!

    _deadline=$(( $(date +%s) + 15 ))
    while :; do
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo "✗ Server failed to start"
            sed -n '1,120p' "$_state/server.log"
            exit 1
        fi
        if grep -q "TNT chat server listening" "$_state/server.log"; then
            pass "server started"
            return 0
        fi
        [ "$(date +%s)" -lt "$_deadline" ] || break
        sleep 0.05
    done

    echo "✗ Server did not become ready"
    sed -n '1,120p' "$_state/server.log"
    exit 1
}

tnt_check_server_alive() {
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        pass "$1"
    else
        fail "$1"
        sed -n '1,120p' "$STATE_DIR/server.log"
    fi
}

# Join the room as $1 and run the expect body in $2, with lib.exp sourced so
# the body can use send_wait.  Waiting for what the keys produced is the
# caller's job; this only gets the session to the composing state.
tnt_expect_session() {
    _name=$1
    _body=$2
    _script="$STATE_DIR/$_name.expect"
    cat >"$_script" <<EOF
set timeout 15
source "$PWD/lib.exp"
spawn ssh $(tnt_ssh_opts "$PORT") anonymous@127.0.0.1
expect "): "
send -- "$_name\r"
expect "/help"
$_body
close
EOF
    expect -f "$_script" >"$STATE_DIR/$_name.session.log" 2>&1
}

# Dump what a session actually saw.  Discarding it hides the case where ssh
# never connected at all, which reads identically to a failed assertion.
tnt_dump_session() {
    [ -f "$STATE_DIR/$1.session.log" ] && sed -n '1,60p' "$STATE_DIR/$1.session.log"
}

tnt_summary() {
    echo ""
    echo "PASSED: $PASS"
    echo "FAILED: $FAIL"
    if [ "$FAIL" -eq 0 ]; then
        echo "All tests passed"
        exit 0
    fi
    echo "Some tests failed"
    exit 1
}
