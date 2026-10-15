#!/bin/bash
# Each session starts in the language its SSH client sends with LANG or LC_*.

BIN="../tnt"
PORT=${PORT:-2222}
PASS=0
FAIL=0
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-client-lang-test.XXXXXX")
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

SSH_BASE="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no -o ConnectionAttempts=3 -o ConnectTimeout=15 -o SendEnv=-* -p $PORT"

wait_for_health() {
    for _ in $(seq 1 15); do
        [ "$(ssh -n $SSH_BASE localhost health 2>/dev/null)" = "ok" ] && return 0
        sleep 1
    done
    return 1
}

# Prints which prompt language the session showed: zh, en or none.
prompt_language() {
    local name="$1"
    shift
    local script="$STATE_DIR/$name.expect"
    cat >"$script" <<EOF
set timeout 10
spawn env -u LANG -u LC_ALL -u LC_MESSAGES ssh $SSH_BASE $* localhost
expect {
    "请输入用户名" { puts "\nLANGUAGE=zh"; send -- "\003"; exit 0 }
    "Enter display name" { puts "\nLANGUAGE=en"; send -- "\003"; exit 0 }
    timeout { puts "\nLANGUAGE=none"; exit 0 }
}
EOF
    expect "$script" 2>&1 | sed -n 's/^LANGUAGE=//p' | tr -d '\r'
}

check() {
    local description="$1" want="$2" got="$3"
    if [ "$got" = "$want" ]; then
        echo "✓ $description"
        PASS=$((PASS + 1))
    else
        echo "✗ $description (want $want, got $got)"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== TNT Client Language Tests ==="

TNT_LANG=en TNT_RATE_LIMIT=0 "$BIN" -p "$PORT" -d "$STATE_DIR" >"$STATE_DIR/server.log" 2>&1 &
SERVER_PID=$!
if ! wait_for_health; then
    echo "✗ server did not start"
    cat "$STATE_DIR/server.log"
    exit 1
fi

check "LANG=zh_CN.UTF-8 starts in Chinese" zh "$(prompt_language zh -o SetEnv=LANG=zh_CN.UTF-8)"
check "LC_ALL wins over LANG" zh "$(prompt_language lcall '-o "SetEnv=LANG=en_US.UTF-8 LC_ALL=zh_CN.UTF-8"')"
check "LANG=en_US.UTF-8 keeps English" en "$(prompt_language en -o SetEnv=LANG=en_US.UTF-8)"
check "an unknown locale falls back to the server default" en "$(prompt_language other -o SetEnv=LANG=fr_FR.UTF-8)"
check "no locale falls back to the server default" en "$(prompt_language none)"

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed"
exit "$FAIL"
