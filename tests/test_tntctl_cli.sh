#!/bin/sh
# Local CLI-shape tests for tntctl. Uses a fake ssh in PATH.

set -u

PASS=0
FAIL=0
BIN="../tntctl"
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tntctl-cli-test.XXXXXX")
FAKE_BIN="${STATE_DIR}/bin"
SSH_LOG="${STATE_DIR}/ssh.argv"

cleanup() {
    rm -rf "$STATE_DIR"
}
trap cleanup EXIT

mkdir -p "$FAKE_BIN"
cat >"$FAKE_BIN/ssh" <<'FAKESSH'
#!/bin/sh
printf '%s\n' "$#" > "$TNTCTL_SSH_LOG"
for arg in "$@"; do
    printf '%s\n' "$arg" >> "$TNTCTL_SSH_LOG"
done
case "$*" in
    *" users --xml") exit 64 ;;
    *) printf 'fake-ok\n'; exit 0 ;;
esac
FAKESSH
chmod +x "$FAKE_BIN/ssh"

run_ok() {
    label=$1
    shift
    : > "$SSH_LOG"
    PATH="$FAKE_BIN:$PATH" TNTCTL_SSH_LOG="$SSH_LOG" "$@" >/dev/null 2>&1
    status=$?
    if [ "$status" -eq 0 ]; then
        echo "✓ $label"
        PASS=$((PASS + 1))
    else
        echo "✗ $label (exit $status)"
        FAIL=$((FAIL + 1))
    fi
}

run_usage() {
    label=$1
    shift
    rm -f "$SSH_LOG"
    PATH="$FAKE_BIN:$PATH" TNTCTL_SSH_LOG="$SSH_LOG" "$@" >/dev/null 2>&1
    status=$?
    if [ "$status" -eq 64 ] && [ ! -f "$SSH_LOG" ]; then
        echo "✓ $label"
        PASS=$((PASS + 1))
    else
        echo "✗ $label (exit $status)"
        [ -f "$SSH_LOG" ] && echo "fake ssh was invoked"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== TNTCTL CLI Tests ==="

if [ ! -x "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

VERSION_OUTPUT=$("$BIN" --version 2>/dev/null || true)
case "$VERSION_OUTPUT" in
    "tntctl "*) echo "✓ version prints"; PASS=$((PASS + 1)) ;;
    *) echo "✗ version output unexpected: $VERSION_OUTPUT"; FAIL=$((FAIL + 1)) ;;
esac

run_ok "basic argv shape" "$BIN" -p 2222 example.com health
grep -q '^example.com$' "$SSH_LOG" &&
grep -q '^health$' "$SSH_LOG"
if [ $? -eq 0 ]; then
    echo "✓ fake ssh receives host and command as separate argv"
    PASS=$((PASS + 1))
else
    echo "✗ fake ssh argv unexpected"
    cat "$SSH_LOG"
    FAIL=$((FAIL + 1))
fi

run_ok "bounded host-key options are passed safely" "$BIN" --host-key-checking accept-new --known-hosts "$STATE_DIR/known_hosts" example.com health
grep -q '^StrictHostKeyChecking=accept-new$' "$SSH_LOG" &&
grep -q "^UserKnownHostsFile=$STATE_DIR/known_hosts$" "$SSH_LOG"
if [ $? -eq 0 ]; then
    echo "✓ bounded host-key options are explicit"
    PASS=$((PASS + 1))
else
    echo "✗ bounded host-key options missing"
    cat "$SSH_LOG"
    FAIL=$((FAIL + 1))
fi

run_ok "login builds user@host destination" "$BIN" -l operator example.com post "hello"
grep -q '^operator@example.com$' "$SSH_LOG"
if [ $? -eq 0 ]; then
    echo "✓ login destination is explicit"
    PASS=$((PASS + 1))
else
    echo "✗ login destination unexpected"
    cat "$SSH_LOG"
    FAIL=$((FAIL + 1))
fi

run_ok "dump command is accepted" "$BIN" example.com dump -n 1
grep -q '^dump -n 1$' "$SSH_LOG"
if [ $? -eq 0 ]; then
    echo "✓ dump argv is forwarded as one remote command"
    PASS=$((PASS + 1))
else
    echo "✗ dump argv unexpected"
    cat "$SSH_LOG"
    FAIL=$((FAIL + 1))
fi

run_ok "remote help alias is accepted" "$BIN" example.com --help
grep -q '^--help$' "$SSH_LOG"
if [ $? -eq 0 ]; then
    echo "✓ --help after host is forwarded as exec help"
    PASS=$((PASS + 1))
else
    echo "✗ remote --help command unexpected"
    cat "$SSH_LOG"
    FAIL=$((FAIL + 1))
fi

PATH="$FAKE_BIN:$PATH" TNTCTL_SSH_LOG="$SSH_LOG" "$BIN" example.com users --xml >/dev/null 2>&1
REMOTE_STATUS=$?
if [ "$REMOTE_STATUS" -eq 64 ]; then
    echo "✓ remote usage status is preserved"
    PASS=$((PASS + 1))
else
    echo "✗ remote usage status unexpected: $REMOTE_STATUS"
    FAIL=$((FAIL + 1))
fi

run_usage "rejects login starting with dash" "$BIN" -l -V example.com health
run_usage "rejects host starting with dash" "$BIN" -bad health
run_usage "rejects unknown command locally" "$BIN" example.com 'health;id'
run_usage "rejects newline command arg locally" "$BIN" example.com post "hello
world"
run_usage "rejects arbitrary ssh option" "$BIN" --ssh-option ProxyCommand=id example.com health
run_usage "rejects invalid host-key mode" "$BIN" --host-key-checking maybe example.com health

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
