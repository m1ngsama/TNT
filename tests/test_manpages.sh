#!/bin/sh

set -u

PASS=0
FAIL=0
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd)
set -- tntctl.1 tnt-message-log.5 tnt-chat.7 tnt-exec.7 \
    tnt-module-protocol.7 tnt.8

cd "$REPO_ROOT" || exit 1

pass() {
    echo "✓ $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "✗ $1"
    if [ -n "${2:-}" ]; then
        printf '%s\n' "$2"
    fi
    FAIL=$((FAIL + 1))
}

require_fixed() {
    file=$1
    text=$2
    label=$3

    if grep -F -q "$text" "$REPO_ROOT/$file"; then
        pass "$label"
    else
        fail "$label" "$file: missing $text"
    fi
}

echo "=== TNT Manual Page Tests ==="

# Reference documentation lives in manual pages; the repository entry points
# are the only Markdown the tree carries.
markdown=$(find "$REPO_ROOT" -type f -name '*.md' \
    -not -path "$REPO_ROOT/.git/*" \
    -not -path "$REPO_ROOT/README.md" \
    -not -path "$REPO_ROOT/SECURITY.md" -print)
if [ -z "$markdown" ]; then
    pass "Markdown is limited to the repository entry points"
else
    fail "reference documentation belongs in manual pages" "$markdown"
fi

missing_entry=""
for entry in README.md SECURITY.md; do
    [ -f "$REPO_ROOT/$entry" ] || missing_entry="$missing_entry $entry"
done
if [ -z "$missing_entry" ]; then
    pass "repository entry points are present"
else
    fail "repository entry points are missing" "$missing_entry"
fi

missing=""
for page do
    [ -f "$REPO_ROOT/$page" ] || missing="$missing $page"
done
if [ -z "$missing" ]; then
    pass "all installed manual pages exist"
else
    fail "manual pages are missing" "$missing"
fi

if command -v mandoc >/dev/null 2>&1; then
    lint_output=""
    lint_status=0
    for page do
        [ -f "$REPO_ROOT/$page" ] || continue
        output=$(mandoc -T lint "$REPO_ROOT/$page" 2>&1) || lint_status=1
        [ -z "$output" ] || lint_output="$lint_output\n$page:\n$output"
        rendered=$(mandoc -T ascii "$REPO_ROOT/$page" 2>/dev/null) || lint_status=1
        [ -n "$rendered" ] || lint_status=1
    done
    if [ "$lint_status" -eq 0 ] && [ -z "$lint_output" ]; then
        pass "mandoc accepts and renders every manual page"
    else
        fail "mandoc reported manual page errors" "$lint_output"
    fi
else
    fail "mandoc is required to validate manual pages"
fi

bad_macros=$(grep -n -E '^\.(TS|TE|nf|fi)([[:space:]]|$)' \
    "$@" 2>/dev/null || true)
if [ -z "$bad_macros" ]; then
    pass "manual pages use portable man macros without tbl or raw fill blocks"
else
    fail "manual pages contain disallowed formatting macros" "$bad_macros"
fi

long_lines=$(awk 'length($0) > 80 { print FILENAME ":" FNR ":" length($0) }' \
    "$@" 2>/dev/null || true)
if [ -z "$long_lines" ]; then
    pass "manual page source lines stay within 80 columns"
else
    fail "manual page source contains lines over 80 columns" "$long_lines"
fi

dead_refs=$(grep -n -E '([[:alnum:]_./-]+\.md|docs/)' \
    "$@" 2>/dev/null || true)
if [ -z "$dead_refs" ]; then
    pass "manual pages contain no Markdown or docs-directory references"
else
    fail "manual pages contain dead documentation references" "$dead_refs"
fi

require_fixed "tntctl.1" '.TH TNTCTL 1 ' "tntctl is a section 1 command"
require_fixed "tnt-message-log.5" '.TH TNT-MESSAGE-LOG 5 ' \
    "message log is a section 5 format"
require_fixed "tnt-chat.7" '.TH TNT-CHAT 7 ' "chat interface is a section 7 overview"
require_fixed "tnt-exec.7" '.TH TNT-EXEC 7 ' "exec interface is a section 7 protocol"
require_fixed "tnt-module-protocol.7" '.TH TNT-MODULE-PROTOCOL 7 ' \
    "module interface is a section 7 protocol"
require_fixed "tnt.8" '.TH TNT 8 ' "server is a section 8 command"

if grep -R -F ':support' "$REPO_ROOT/src" "$REPO_ROOT/include" \
    "$@" >/dev/null 2>&1; then
    fail "removed support command is still advertised"
else
    pass "removed support command is not advertised"
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
