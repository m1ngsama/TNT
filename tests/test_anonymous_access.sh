#!/bin/bash
# Test anonymous SSH access

BIN="../tnt"
PORT=${PORT:-2222}

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found."
    exit 1
fi

echo "Starting TNT server on port $PORT..."
$BIN -p $PORT > /dev/null 2>&1 &
SERVER_PID=$!
sleep 2

cleanup() {
    kill $SERVER_PID 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT

echo "Testing anonymous SSH access to TNT server..."
echo ""

# Test 1: Connection with any username and password
echo "Test 1: Connection with any username (should succeed)"
timeout 10 expect -c "
spawn ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p $PORT testuser@localhost
expect {
    \"password:\" {
        send \"anypassword\r\"
        expect {
            \"请输入用户名\" {
                send \"TestUser\r\"
                send \"\003\"
                exit 0
            }
            timeout { exit 1 }
        }
    }
    timeout { exit 1 }
}
" 2>&1 | grep -q "请输入用户名"

if [ $? -eq 0 ]; then
    echo "✓ Test 1 PASSED: Can connect with any password"
else
    echo "✗ Test 1 FAILED: Cannot connect with any password"
    exit 1
fi

echo ""

# Test 2: Connection should work with empty password
echo "Test 2: Simple connection (standard SSH command)"
timeout 10 expect -c "
spawn ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p $PORT anonymous@localhost
expect {
    \"password:\" {
        send \"\r\"
        expect {
            \"请输入用户名\" {
                send \"\r\"
                send \"\003\"
                exit 0
            }
            timeout { exit 1 }
        }
    }
    timeout { exit 1 }
}
" 2>&1 | grep -q "请输入用户名"

if [ $? -eq 0 ]; then
    echo "✓ Test 2 PASSED: Can connect with empty password"
else
    echo "✗ Test 2 FAILED: Cannot connect with empty password"
    exit 1
fi

echo ""
echo "Anonymous access test completed."
exit 0
