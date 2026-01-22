#!/bin/bash
# TNT Health Check Script
# Verifies the server is running and accepting connections

PORT="${1:-2222}"
TIMEOUT="${2:-5}"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "TNT Health Check"
echo "=================="
echo ""

# Check if process is running
echo -n "Process check: "
if pgrep -x tnt > /dev/null; then
    echo -e "${GREEN}✓${NC} TNT process is running"
    PID=$(pgrep -x tnt)
    echo "  PID: $PID"
else
    echo -e "${RED}✗${NC} TNT process is not running"
    exit 1
fi

# Check if port is listening
echo -n "Port check: "
if lsof -i:$PORT -sTCP:LISTEN > /dev/null 2>&1 || netstat -ln 2>/dev/null | grep -q ":$PORT "; then
    echo -e "${GREEN}✓${NC} Port $PORT is listening"
else
    echo -e "${RED}✗${NC} Port $PORT is not listening"
    exit 1
fi

# Try to connect via SSH
echo -n "Connection test: "
timeout $TIMEOUT ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=$TIMEOUT -p $PORT test@localhost exit 2>/dev/null &
CONNECT_PID=$!
wait $CONNECT_PID
CONNECT_RESULT=$?

if [ $CONNECT_RESULT -eq 0 ] || [ $CONNECT_RESULT -eq 255 ]; then
    # Exit code 255 means SSH connection was established but auth failed, which is expected
    echo -e "${GREEN}✓${NC} SSH connection successful"
else
    echo -e "${YELLOW}⚠${NC} SSH connection timeout or failed (but port is listening)"
fi

# Check log file
LOG_FILE="/var/lib/tnt/messages.log"
if [ -f "$LOG_FILE" ]; then
    LOG_SIZE=$(du -h "$LOG_FILE" | cut -f1)
    echo "Log file: $LOG_SIZE"
else
    LOG_FILE="./messages.log"
    if [ -f "$LOG_FILE" ]; then
        LOG_SIZE=$(du -h "$LOG_FILE" | cut -f1)
        echo "Log file: $LOG_SIZE"
    else
        echo "Log file: Not found (will be created on first message)"
    fi
fi

# Memory usage
echo -n "Memory usage: "
if [ ! -z "$PID" ]; then
    MEM=$(ps -p $PID -o rss= | awk '{printf "%.1f MB", $1/1024}')
    echo "$MEM"
fi

echo ""
echo -e "${GREEN}Health check passed${NC}"
exit 0
