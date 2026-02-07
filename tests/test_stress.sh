#!/bin/sh
# Stress test for TNT server
# Usage: ./test_stress.sh [num_clients]

PORT=${PORT:-2222}
CLIENTS=${1:-10}
DURATION=${2:-30}
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

echo "Starting TNT server on port $PORT..."
$BIN -p $PORT &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Server failed to start"
    exit 1
fi

echo "Spawning $CLIENTS clients for ${DURATION}s..."

for i in $(seq 1 $CLIENTS); do
    (
        sleep $((i % 5))
        echo "test user $i" | $TIMEOUT_CMD $DURATION ssh -o StrictHostKeyChecking=no \
            -o UserKnownHostsFile=/dev/null -p $PORT localhost \
            >/dev/null 2>&1
    ) &
done

echo "Running stress test..."
sleep $DURATION

echo "Cleaning up..."
kill $SERVER_PID 2>/dev/null
wait

echo "Stress test complete"
if ps aux | grep tnt | grep -v grep > /dev/null; then
    echo "WARNING: tnt process still running"
else
    echo "Server shutdown confirmed."
fi

exit 0
