#!/bin/bash
# Anonymous SSH access regression tests for TNT

BIN="../tnt"
PORT=${PORT:-2222}
PASS=0
FAIL=0
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-anonymous-test.XXXXXX")
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
    echo "Error: Binary $BIN not found."
    exit 1
fi

SSH_BASE="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no -o ConnectionAttempts=3 -o ConnectTimeout=15 -p $PORT"

wait_for_health() {
    local out

    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
        if [ -n "$SERVER_PID" ] && ! kill -0 "$SERVER_PID" 2>/dev/null; then
            return 1
        fi
        out=$(ssh -n $SSH_BASE localhost health 2>/dev/null || true)
        [ "$out" = "ok" ] && return 0
        sleep 1
    done
    return 1
}

run_password_test() {
    local name="$1"
    local user="$2"
    local password="$3"
    local display_name="$4"
    local script="$STATE_DIR/$name.expect"
    local log="$STATE_DIR/$name.log"

    cat >"$script" <<EOF
set timeout 10
spawn ssh $SSH_BASE -o PreferredAuthentications=password $user@localhost
expect {
    -re "(?i)password:" {
        send -- "$password\r"
        exp_continue
    }
    "请输入用户名" {
        send -- "$display_name\r"
        send -- "\003"
        expect eof
        exit 0
    }
    timeout { exit 1 }
    eof { exit 1 }
}
EOF

    if expect "$script" >"$log" 2>&1; then
        return 0
    fi

    sed -n '1,120p' "$log"
    return 1
}

echo "=== TNT Anonymous Access Tests ==="

TNT_LANG=zh TNT_RATE_LIMIT=0 TNT_MAX_CONN_PER_IP=256 TNT_MAX_CONNECTIONS=256 "$BIN" -p "$PORT" -d "$STATE_DIR" \
    >"$STATE_DIR/server.log" 2>&1 &
SERVER_PID=$!

if wait_for_health; then
    echo "✓ server started"
    PASS=$((PASS + 1))
else
    echo "✗ server failed to start"
    sed -n '1,120p' "$STATE_DIR/server.log"
    exit 1
fi

if run_password_test "any-password" "testuser" "anypassword" "TestUser"; then
    echo "✓ accepts any password when no access token is configured"
    PASS=$((PASS + 1))
else
    echo "✗ any-password login failed"
    FAIL=$((FAIL + 1))
fi

if run_password_test "empty-password" "anonymous" "" ""; then
    echo "✓ accepts empty password when no access token is configured"
    PASS=$((PASS + 1))
else
    echo "✗ empty-password login failed"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
