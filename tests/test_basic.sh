#!/bin/sh
# Basic functional tests
# Usage: ./test_basic.sh

PORT=${PORT:-2222}
PASS=0
FAIL=0

cleanup() {
    kill $SERVER_PID 2>/dev/null
    rm -f test.log
}

trap cleanup EXIT

echo "=== TNT Basic Tests ==="

# Path to binary
BIN="../tnt"

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

# Start server
$BIN -p $PORT >test.log 2>&1 &
SERVER_PID=$!
sleep 2

# Test 1: Server started
if kill -0 $SERVER_PID 2>/dev/null; then
    echo "✓ Server started"
    PASS=$((PASS + 1))
else
    echo "✗ Server failed to start"
    FAIL=$((FAIL + 1))
    exit 1
fi

# Test 2: SSH connection
if timeout 5 ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o BatchMode=yes -p $PORT localhost exit 2>/dev/null; then
    echo "✓ SSH connection works"
    PASS=$((PASS + 1))
else
    echo "✗ SSH connection failed"
    FAIL=$((FAIL + 1))
fi

# Test 3: Message logging
(echo "testuser"; echo "test message"; sleep 1) | timeout 5 ssh -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null -p $PORT localhost >/dev/null 2>&1 &
sleep 3
if [ -f messages.log ]; then
    echo "✓ Message logging works"
    PASS=$((PASS + 1))
else
    echo "✗ Message logging failed"
    FAIL=$((FAIL + 1))
fi

# Summary
echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ $FAIL -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit $FAIL
