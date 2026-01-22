#!/bin/bash
# Test anonymous SSH access

echo "Testing anonymous SSH access to TNT server..."
echo ""

# Test 1: Connection with any username and password
echo "Test 1: Connection with any username (should succeed)"
timeout 5 expect -c '
spawn ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2223 testuser@localhost
expect {
    "password:" {
        send "anypassword\r"
        expect {
            "请输入用户名:" {
                send "TestUser\r"
                send "\003"
                exit 0
            }
            timeout { exit 1 }
        }
    }
    timeout { exit 1 }
}
' 2>&1 | grep -q "请输入用户名"

if [ $? -eq 0 ]; then
    echo "✓ Test 1 PASSED: Can connect with any password"
else
    echo "✗ Test 1 FAILED: Cannot connect with any password"
fi

echo ""

# Test 2: Connection should work without special SSH options
echo "Test 2: Simple connection (standard SSH command)"
timeout 5 expect -c '
spawn ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2223 anonymous@localhost
expect {
    "password:" {
        send "\r"
        expect {
            "请输入用户名:" {
                send "\r"
                send "\003"
                exit 0
            }
            timeout { exit 1 }
        }
    }
    timeout { exit 1 }
}
' 2>&1 | grep -q "请输入用户名"

if [ $? -eq 0 ]; then
    echo "✓ Test 2 PASSED: Can connect with empty password"
else
    echo "✗ Test 2 FAILED: Cannot connect with empty password"
fi

echo ""
echo "Anonymous access test completed."
