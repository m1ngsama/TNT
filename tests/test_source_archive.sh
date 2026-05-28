#!/bin/sh
# Release source-archive regression tests.

set -u

PASS=0
FAIL=0
SCRIPT="../scripts/package_source_archive.sh"
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-source-archive-test.XXXXXX")

cleanup() {
    rm -rf "$STATE_DIR"
}
trap cleanup EXIT

pass() {
    echo "✓ $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "✗ $1"
    if [ "${2:-}" ]; then
        printf '%s\n' "$2"
    fi
    FAIL=$((FAIL + 1))
}

version() {
    sed -n 's/^#define TNT_VERSION "\([^"]*\)".*/\1/p' ../include/common.h
}

listing_has_entry() {
    entry=$1

    printf '%s\n' "$ARCHIVE_LISTING" |
        awk -v target="$entry" '$0 == target { found = 1 } END { exit found ? 0 : 1 }'
}

echo "=== TNT Source Archive Tests ==="

if [ ! -x "$SCRIPT" ]; then
    echo "Error: script $SCRIPT not found or not executable."
    exit 1
fi

VER=$(version)
OUT_DIR="$STATE_DIR/out"
OUTPUT=$("$SCRIPT" HEAD "$OUT_DIR" 2>&1)
STATUS=$?
ARCHIVE="$OUT_DIR/tnt-chat-v$VER-source.tar.gz"

if [ "$STATUS" -eq 0 ] &&
   [ "$OUTPUT" = "$ARCHIVE" ] &&
   [ -s "$ARCHIVE" ]; then
    pass "HEAD source archive is built"
else
    fail "HEAD source archive build" "$OUTPUT"
fi

ARCHIVE_LISTING=$(tar -tzf "$ARCHIVE" 2>&1)
if listing_has_entry "TNT-$VER/LICENSE" &&
   listing_has_entry "TNT-$VER/src/tntctl.c" &&
   listing_has_entry "TNT-$VER/packaging/README.md" &&
   listing_has_entry "TNT-$VER/tnt.1" &&
   listing_has_entry "TNT-$VER/tntctl.1"; then
    pass "source archive contains required release files"
else
    fail "source archive required files" "$(printf '%s\n' "$ARCHIVE_LISTING" | sed -n '1,40p')"
fi

DUP_OUTPUT=$("$SCRIPT" HEAD "$OUT_DIR" 2>&1)
DUP_STATUS=$?
if [ "$DUP_STATUS" -ne 0 ] &&
   printf '%s\n' "$DUP_OUTPUT" | grep -q 'output already exists'; then
    pass "existing archive is not overwritten"
else
    fail "existing archive handling" "$DUP_OUTPUT"
fi

BAD_OUTPUT=$("$SCRIPT" refs/heads/does-not-exist "$STATE_DIR/bad" 2>&1)
BAD_STATUS=$?
if [ "$BAD_STATUS" -ne 0 ] &&
   printf '%s\n' "$BAD_OUTPUT" | grep -q 'could not resolve git ref'; then
    pass "missing git ref is rejected"
else
    fail "missing git ref handling" "$BAD_OUTPUT"
fi

HELP_OUTPUT=$("$SCRIPT" --help 2>&1)
HELP_STATUS=$?
if [ "$HELP_STATUS" -eq 0 ] &&
   printf '%s\n' "$HELP_OUTPUT" | grep -q 'Usage: scripts/package_source_archive.sh REF'; then
    pass "help output is available"
else
    fail "help output handling" "$HELP_OUTPUT"
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
