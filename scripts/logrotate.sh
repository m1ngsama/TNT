#!/bin/bash
# TNT Log Rotation Script
# Keeps chat history manageable and prevents disk space issues

LOG_FILE="${1:-/var/lib/tnt/messages.log}"
MAX_SIZE_MB="${2:-100}"
KEEP_LINES="${3:-10000}"

# Check if log file exists
if [ ! -f "$LOG_FILE" ]; then
    echo "Log file $LOG_FILE does not exist"
    exit 0
fi

# Get file size in MB
FILE_SIZE=$(du -m "$LOG_FILE" | cut -f1)

# Rotate if file is too large
if [ "$FILE_SIZE" -gt "$MAX_SIZE_MB" ]; then
    echo "Log file size: ${FILE_SIZE}MB, rotating..."

    # Create backup
    BACKUP="${LOG_FILE}.$(date +%Y%m%d_%H%M%S)"
    cp "$LOG_FILE" "$BACKUP"

    # Keep only last N lines
    tail -n "$KEEP_LINES" "$LOG_FILE" > "${LOG_FILE}.tmp"
    mv "${LOG_FILE}.tmp" "$LOG_FILE"

    # Compress old backup
    gzip "$BACKUP"

    echo "Log rotated. Backup: ${BACKUP}.gz"
    echo "Kept last $KEEP_LINES lines"
else
    echo "Log file size: ${FILE_SIZE}MB (under ${MAX_SIZE_MB}MB limit)"
fi

# Clean up old compressed logs (keep last 5)
LOG_DIR=$(dirname "$LOG_FILE")
cd "$LOG_DIR" || exit
ls -t messages.log.*.gz 2>/dev/null | tail -n +6 | xargs rm -f 2>/dev/null

echo "Log rotation complete"
