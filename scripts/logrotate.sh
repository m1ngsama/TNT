#!/bin/sh
# Compact and archive a TNT messages.log file.
#
# This is an operator-run maintenance tool.  For strict consistency, stop TNT
# or run it during a quiet maintenance window before compacting the active log.

set -eu

DRY_RUN=0
KEEP_ARCHIVES=5

usage() {
    cat <<'USAGE'
Usage: scripts/logrotate.sh [--dry-run] [--keep-archives N] [LOG_FILE [MAX_SIZE_MB [KEEP_LINES]]]

Defaults:
  LOG_FILE       /var/lib/tnt/messages.log
  MAX_SIZE_MB    100
  KEEP_LINES     10000

Exit status:
  0   success, including missing log file
  1   runtime error
  64  invalid arguments
USAGE
}

fail_usage() {
    echo "logrotate: $*" >&2
    usage >&2
    exit 64
}

fail() {
    echo "logrotate: $*" >&2
    exit 1
}

is_uint() {
    case "${1:-}" in
        ''|*[!0-9]*)
            return 1
            ;;
        *)
            return 0
            ;;
    esac
}

is_positive_uint() {
    is_uint "$1" && [ "$1" -gt 0 ]
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        --keep-archives)
            [ "$#" -ge 2 ] || fail_usage "missing value for --keep-archives"
            is_uint "$2" || fail_usage "invalid archive count: $2"
            KEEP_ARCHIVES=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        -*)
            fail_usage "unknown option: $1"
            ;;
        *)
            break
            ;;
    esac
done

[ "$#" -le 3 ] || fail_usage "too many arguments"

LOG_FILE=${1:-/var/lib/tnt/messages.log}
MAX_SIZE_MB=${2:-100}
KEEP_LINES=${3:-10000}

case "$LOG_FILE" in
    ''|-*)
        fail_usage "invalid log path"
        ;;
esac
is_uint "$MAX_SIZE_MB" || fail_usage "invalid max size: $MAX_SIZE_MB"
is_positive_uint "$KEEP_LINES" || fail_usage "invalid keep lines: $KEEP_LINES"

if [ ! -e "$LOG_FILE" ]; then
    echo "logrotate: $LOG_FILE does not exist"
    exit 0
fi
[ -f "$LOG_FILE" ] || fail "$LOG_FILE is not a regular file"

MAX_BYTES=$((MAX_SIZE_MB * 1024 * 1024))
FILE_SIZE=$(wc -c < "$LOG_FILE" | tr -d ' ')
[ -n "$FILE_SIZE" ] || fail "could not read log size"

compact_log() {
    timestamp=$(date -u +%Y%m%dT%H%M%SZ)
    backup="${LOG_FILE}.${timestamp}"
    suffix=1

    while [ -e "$backup" ] || [ -e "${backup}.gz" ]; do
        backup="${LOG_FILE}.${timestamp}.${suffix}"
        suffix=$((suffix + 1))
    done

    if [ "$DRY_RUN" -eq 1 ]; then
        echo "logrotate: would archive $LOG_FILE to $backup"
        echo "logrotate: would keep last $KEEP_LINES lines"
        return 0
    fi

    tmp="${LOG_FILE}.tmp.$$"
    rm -f "$tmp"
    cp -p "$LOG_FILE" "$backup" || fail "failed to create archive"
    if ! tail -n "$KEEP_LINES" "$LOG_FILE" > "$tmp"; then
        rm -f "$tmp"
        fail "failed to compact log"
    fi
    if ! cat "$tmp" > "$LOG_FILE"; then
        rm -f "$tmp"
        fail "failed to replace log"
    fi
    rm -f "$tmp"

    if command -v gzip >/dev/null 2>&1; then
        gzip -f "$backup" || fail "failed to compress archive"
        backup="${backup}.gz"
    fi

    echo "logrotate: archived $backup"
    echo "logrotate: kept last $KEEP_LINES lines"
}

cleanup_archives() {
    [ "$KEEP_ARCHIVES" -ge 0 ] || return 0

    archives=$(
        ls -1t "$LOG_FILE".*.gz "$LOG_FILE".[0-9]* 2>/dev/null || true
    )
    [ -n "$archives" ] || return 0

    printf '%s\n' "$archives" |
    awk '!seen[$0]++' |
    awk -v keep="$KEEP_ARCHIVES" 'NR > keep' |
    while IFS= read -r old; do
        [ -n "$old" ] || continue
        if [ "$DRY_RUN" -eq 1 ]; then
            echo "logrotate: would remove $old"
        else
            rm -f "$old"
        fi
    done
}

if [ "$FILE_SIZE" -gt "$MAX_BYTES" ]; then
    echo "logrotate: size ${FILE_SIZE} bytes exceeds ${MAX_BYTES} bytes"
    compact_log
else
    echo "logrotate: size ${FILE_SIZE} bytes is within ${MAX_BYTES} bytes"
fi

cleanup_archives
echo "logrotate: complete"
