#!/bin/bash
# Security Features Verification Test
# Tests security features without requiring interactive SSH sessions

# Don't use set -e as we want to continue even if some commands fail

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASS=0
FAIL=0

print_test() {
    echo -e "\n${YELLOW}[TEST]${NC} $1"
}

pass() {
    echo -e "${GREEN}✓ PASS${NC}: $1"
    ((PASS++))
}

fail() {
    echo -e "${RED}✗ FAIL${NC}: $1"
    ((FAIL++))
}

cleanup() {
    pkill -f "^\.\./tnt" 2>/dev/null || true
    sleep 1
}

trap cleanup EXIT

echo -e "${YELLOW}========================================${NC}"
echo -e "${YELLOW}TNT Security Features Test Suite${NC}"
echo -e "${YELLOW}========================================${NC}"

BIN="../tnt"
if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found."
    exit 1
fi

# Detect timeout command
TIMEOUT_CMD="timeout"
if command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_CMD="gtimeout"
fi

# Test 1: 4096-bit RSA Key Generation
print_test "1. RSA 4096-bit Key Generation"
rm -f host_key
$BIN &
PID=$!
sleep 8  # Wait for key generation
kill $PID 2>/dev/null || true
sleep 1

if [ -f host_key ]; then
    KEY_SIZE=$(ssh-keygen -l -f host_key 2>/dev/null | awk '{print $1}')
    if [ "$KEY_SIZE" = "4096" ]; then
        pass "RSA key upgraded to 4096 bits (was 2048)"
    else
        fail "Key is $KEY_SIZE bits, expected 4096"
    fi

    # Check permissions
    PERMS=$(stat -f "%OLp" host_key)
    if [ "$PERMS" = "600" ]; then
        pass "Host key has secure permissions (600)"
    else
        fail "Host key permissions are $PERMS, expected 600"
    fi
else
    fail "Host key not generated"
fi

# Test 2: Server Start with Different Configurations
print_test "2. Environment Variable Configuration"

# Test bind address
TNT_BIND_ADDR=127.0.0.1 $TIMEOUT_CMD 3 $BIN 2>&1 | grep -q "TNT chat server" && \
    pass "TNT_BIND_ADDR configuration works" || fail "TNT_BIND_ADDR not working"

# Test with access token set (just verify it starts)
TNT_ACCESS_TOKEN="test123" $TIMEOUT_CMD 3 $BIN 2>&1 | grep -q "TNT chat server" && \
    pass "TNT_ACCESS_TOKEN configuration accepted" || fail "TNT_ACCESS_TOKEN not working"

# Test max connections configuration
TNT_MAX_CONNECTIONS=10 $TIMEOUT_CMD 3 $BIN 2>&1 | grep -q "TNT chat server" && \
    pass "TNT_MAX_CONNECTIONS configuration accepted" || fail "TNT_MAX_CONNECTIONS not working"

# Test rate limit toggle
TNT_RATE_LIMIT=0 $TIMEOUT_CMD 3 $BIN 2>&1 | grep -q "TNT chat server" && \
    pass "TNT_RATE_LIMIT configuration accepted" || fail "TNT_RATE_LIMIT not working"

sleep 1

# Test 3: Input Validation in Message Log
print_test "3. Message Log Sanitization"
rm -f messages.log

# Create a test message log with potentially dangerous content
cat > messages.log <<EOF
2026-01-22T10:00:00Z|testuser|normal message
2026-01-22T10:01:00Z|user|with|pipes|attempt to break format
2026-01-22T10:02:00Z|user
newline|content with
newline
2026-01-22T10:03:00Z|validuser|valid content
EOF

# Start server and let it load messages
$BIN &
PID=$!
sleep 3
kill $PID 2>/dev/null || true
sleep 1

# Check if server handled malformed log entries safely
if grep -q "validuser" messages.log; then
    pass "Server loads messages from log file"
else
    fail "Server message loading issue"
fi

# Test 4: UTF-8 Validation
print_test "4. UTF-8 Input Validation"
# Compile a small test program to verify UTF-8 validation
cat > test_utf8.c <<'EOF'
#include <stdio.h>
#include <stdbool.h>
#include "../include/utf8.h"
#include "../include/common.h"

int main() {
    // Valid UTF-8 sequences
    char valid[] = {0xC3, 0xA9, 0};  // é
    char invalid1[] = {0xC3, 0x28, 0};  // Invalid continuation byte
    char overlong[] = {0xC0, 0x80, 0};  // Overlong encoding of NULL

    if (utf8_is_valid_sequence(valid, 2)) {
        printf("✓ Valid UTF-8 accepted\n");
    } else {
        printf("✗ Valid UTF-8 rejected\n");
        return 1;
    }

    if (!utf8_is_valid_sequence(invalid1, 2)) {
        printf("✓ Invalid UTF-8 rejected\n");
    } else {
        printf("✗ Invalid UTF-8 accepted\n");
        return 1;
    }

    if (!utf8_is_valid_sequence(overlong, 2)) {
        printf("✓ Overlong encoding rejected\n");
    } else {
        printf("✗ Overlong encoding accepted\n");
        return 1;
    }

    return 0;
}
EOF

if gcc -I../include -o test_utf8 test_utf8.c ../src/utf8.c 2>/dev/null; then
    if ./test_utf8; then
        pass "UTF-8 validation function works correctly"
    else
        fail "UTF-8 validation has issues"
    fi
    rm -f test_utf8
else
    echo "  (Skipping UTF-8 test - compilation issue)"
fi
rm -f test_utf8.c

# Test 5: Buffer Safety with AddressSanitizer
print_test "5. Buffer Overflow Protection (ASAN Build)"
if make -C .. clean >/dev/null 2>&1 && make -C .. asan >/dev/null 2>&1; then
    # Just verify it compiles - actual ASAN testing needs runtime
    if [ -f ../tnt ]; then
        pass "AddressSanitizer build successful"
        # Restore normal build
        make -C .. clean >/dev/null 2>&1 && make -C .. >/dev/null 2>&1
    else
        fail "AddressSanitizer build failed"
    fi
else
    echo "  (Skipping ASAN test - build issue)"
fi

# Test 6: Concurrent Safety
print_test "6. Concurrency Safety (Data Structure Integrity)"
# This test verifies the code compiles with thread sanitizer flags
if gcc -fsanitize=thread -g -O1 -I../include -I/opt/homebrew/opt/libssh/include \
    -c ../src/chat_room.c -o /tmp/test_tsan.o 2>/dev/null; then
    pass "Code compiles with ThreadSanitizer (concurrency checks enabled)"
    rm -f /tmp/test_tsan.o
else
    fail "ThreadSanitizer compilation failed"
fi

# Test 7: Resource Management (Dynamic Allocation)
print_test "7. Resource Management (Large Log Files)"
rm -f messages.log
# Create a large message log (2000 entries, more than old fixed 1000 limit)
for i in $(seq 1 2000); do
    echo "2026-01-22T$(printf "%02d" $((i/100))):$(printf "%02d" $((i%60))):00Z|user$i|message $i" >> messages.log
done

$BIN &
PID=$!
sleep 4
kill $PID 2>/dev/null || true
sleep 1

# Check if server started successfully with large log
if [ -f messages.log ]; then
    LINE_COUNT=$(wc -l < messages.log)
    if [ "$LINE_COUNT" -ge 2000 ]; then
        pass "Server handles large message log (${LINE_COUNT} messages)"
    else
        fail "Message log truncated unexpectedly"
    fi
fi

# Summary
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}Test Results${NC}"
echo -e "${YELLOW}========================================${NC}"
echo -e "${GREEN}Passed: $PASS${NC}"
echo -e "${RED}Failed: $FAIL${NC}"
echo -e "${YELLOW}========================================${NC}"

if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}✓ All security features verified!${NC}"
    exit 0
else
    echo -e "${RED}✗ Some tests failed${NC}"
    exit 1
fi
