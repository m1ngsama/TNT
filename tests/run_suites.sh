#!/bin/sh
# Run the server-launching integration suites concurrently.  Each suite owns a
# port offset, so the only shared resource is the machine itself.
set -u

BASE_PORT=${PORT:-2222}
JOBS=${INTEGRATION_JOBS:-4}

# Slowest first, so the last job slot never waits on a long suite.
SUITES="interactive_input 2
login_timeouts 8
exec_mode 1
user_lifecycle 3
default_keymap 11
graceful_shutdown 7
basic 0
cursor_editing 10
module_runtime 6
multiline_input 14
vim_keymap 12
wide_text_view 9
empty_view 5
mute_joins_view 4
log_migration 13"

WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-suites.XXXXXX")
trap 'rm -rf "$WORK_DIR"' EXIT
printf '%s\n' "$SUITES" >"$WORK_DIR/suites"

export BASE_PORT WORK_DIR
# A suite's own exit status goes to a file so one failure does not stop the
# rest; xargs would abandon the remaining slots.
printf '%s\n' "$SUITES" | xargs -P "$JOBS" -n 2 sh -c '
    PORT=$((BASE_PORT + $2)) "./test_$1.sh" >"$WORK_DIR/$1.log" 2>&1
    echo $? >"$WORK_DIR/$1.status"
' sh

# Replay in list order so a parallel run reads like a serial one.
failed=0
while read -r name _; do
    [ -n "$name" ] || continue
    cat "$WORK_DIR/$name.log" 2>/dev/null || true
    if [ "$(cat "$WORK_DIR/$name.status" 2>/dev/null || echo 1)" -ne 0 ]; then
        echo "x suite test_$name.sh failed"
        failed=1
    fi
    echo ""
done <"$WORK_DIR/suites"

exit "$failed"
