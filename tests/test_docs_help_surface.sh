#!/bin/sh
# Regression checks for active help/manual surfaces.

PASS=0
FAIL=0

pass() {
    echo "✓ $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "✗ $1"
    if [ -n "$2" ]; then
        printf '%s\n' "$2"
    fi
    FAIL=$((FAIL + 1))
}

require_fixed() {
    file="$1"
    text="$2"
    label="$3"

    if grep -F -q "$text" "../$file"; then
        pass "$label"
    else
        fail "$label missing" "$file: $text"
    fi
}

forbid_fixed() {
    file="$1"
    text="$2"
    label="$3"

    if grep -F -q "$text" "../$file"; then
        fail "$label still mentions $text" "$file"
    else
        pass "$label excludes $text"
    fi
}

echo "=== TNT Help Surface Tests ==="

require_fixed "tnt.1" "/	Search message history" "manual documents NORMAL search"
require_fixed "tnt.1" "Space/b	Scroll full page down/up" "manual documents space/b paging"
require_fixed "tnt.1" "PageDown/PageUp	Scroll full page down/up" "manual documents page keys"
require_fixed "tnt.1" "End/Home	Jump to bottom/top" "manual documents end/home"
require_fixed "tnt.1" "g/G	Jump to top/bottom" "manual documents g/G"
require_fixed "tnt.1" ":lang	Show current UI language" "manual documents current language"
require_fixed "tnt.1" ":lang \fIen|zh\fR	Switch UI language for this session" "manual documents language codes"

for file in \
    README.md \
    docs/EASY_SETUP.md \
    docs/DEPLOYMENT.md \
    docs/INTERFACE.md \
    docs/QUICKREF.md \
    docs/USER_LIFECYCLE.md \
    tnt.1 \
    tntctl.1 \
    src/command_catalog.c \
    src/help_text.c \
    src/manual_text.c
do
    forbid_fixed "$file" ":support" "$file"
done

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
