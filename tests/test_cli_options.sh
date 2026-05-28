#!/bin/sh
# CLI option parsing regression tests.

BIN="../tnt"
PASS=0
FAIL=0

pass() {
    echo "✓ $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "✗ $1"
    if [ -n "$2" ]; then
        printf '%s\n' "$2"
    fi
    FAIL=$((FAIL + 1))
}

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

expect_missing_arg() {
    opt="$1"
    output=$("$BIN" "$opt" 2>&1)
    status=$?

    if [ "$status" -eq 64 ] &&
       printf '%s\n' "$output" | grep -q "Option requires argument: $opt"; then
        pass "$opt reports missing argument"
    else
        fail "$opt missing argument diagnostic unexpected" "$output"
    fi
}

echo "=== TNT CLI Option Tests ==="

for opt in \
    -p \
    --port \
    -d \
    --state-dir \
    --bind \
    --public-host \
    --max-connections \
    --max-conn-per-ip \
    --max-conn-rate-per-ip \
    --rate-limit \
    --idle-timeout \
    --ssh-log-level \
    --log-check \
    --log-recover
do
    expect_missing_arg "$opt"
done

ZH_OUTPUT=$(TNT_LANG=zh "$BIN" --bind 2>&1)
ZH_STATUS=$?
if [ "$ZH_STATUS" -eq 64 ] &&
   printf '%s\n' "$ZH_OUTPUT" | grep -q '选项需要参数: --bind'; then
    pass "missing argument diagnostic follows TNT_LANG"
else
    fail "localized missing argument diagnostic unexpected" "$ZH_OUTPUT"
fi

BAD_PORT_OUTPUT=$("$BIN" --port abc 2>&1)
BAD_PORT_STATUS=$?
if [ "$BAD_PORT_STATUS" -eq 64 ] &&
   printf '%s\n' "$BAD_PORT_OUTPUT" | grep -q 'Invalid port: abc'; then
    pass "invalid port still reports invalid value"
else
    fail "invalid port diagnostic unexpected" "$BAD_PORT_OUTPUT"
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
