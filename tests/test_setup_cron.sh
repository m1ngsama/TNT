#!/bin/sh
# Regression tests for the maintenance cron privilege boundary.

set -u

PASS=0
FAIL=0
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
SCRIPT="$ROOT/scripts/setup_cron.sh"
STAGE=$(mktemp -d "${TMPDIR:-/tmp}/tnt-setup-cron-test.XXXXXX")

# shellcheck disable=SC2329 # Invoked through trap.
cleanup() {
    rm -rf "$STAGE"
}
trap cleanup EXIT INT TERM

pass() {
    echo "PASS $1"
    PASS=$((PASS + 1))
}

fail_case() {
    echo "FAIL $1"
    FAIL=$((FAIL + 1))
}

mode_of() {
    stat -c '%a' "$1" 2>/dev/null || stat -f '%Lp' "$1"
}

echo "=== TNT Setup Cron Tests ==="

# A pre-existing service-owned script tree is deliberately left in place.  It
# must never influence the installed code or the generated cron configuration.
mkdir -p "$STAGE/var/lib/tnt/scripts"
printf '#!/bin/sh\necho untrusted\n' > "$STAGE/var/lib/tnt/scripts/logrotate.sh"

if DESTDIR="$STAGE" "$SCRIPT" >"$STAGE/install.out" 2>&1; then
    pass "staged installation succeeds without root"
else
    fail_case "staged installation succeeds without root"
    sed -n '1,120p' "$STAGE/install.out"
fi

LIBEXEC="$STAGE/usr/local/libexec/tnt"
CRON="$STAGE/etc/cron.d/tnt"
if [ -x "$LIBEXEC/logrotate.sh" ] &&
   [ -x "$LIBEXEC/healthcheck.sh" ] &&
   ! grep -q 'echo untrusted' "$LIBEXEC/logrotate.sh"; then
    pass "trusted scripts install outside the writable state directory"
else
    fail_case "trusted scripts install outside the writable state directory"
fi

if [ "$(mode_of "$LIBEXEC")" = 755 ] &&
   [ "$(mode_of "$LIBEXEC/logrotate.sh")" = 755 ] &&
   [ "$(mode_of "$LIBEXEC/healthcheck.sh")" = 755 ] &&
   [ "$(mode_of "$CRON")" = 644 ] &&
   [ "$(mode_of "$STAGE/var/log/tnt-logrotate.log")" = 660 ] &&
   [ "$(mode_of "$STAGE/var/log/tnt-health.log")" = 660 ]; then
    pass "installed files use restricted modes"
else
    fail_case "installed files use restricted modes"
fi

if grep -Fq '0 3 * * * tnt /usr/local/libexec/tnt/logrotate.sh /var/lib/tnt/messages.log 100 10000' "$CRON" &&
   grep -Fq '*/5 * * * * tnt /usr/local/libexec/tnt/healthcheck.sh 2222 5' "$CRON" &&
   ! grep -Fq '/var/lib/tnt/scripts' "$CRON" &&
   ! grep -Eq '^[^#].* root ' "$CRON"; then
    pass "cron executes protected scripts as the service user"
else
    fail_case "cron executes protected scripts as the service user"
    cat "$CRON" 2>/dev/null
fi

printf 'preserve me\n' > "$STAGE/var/log/tnt-health.log"
if DESTDIR="$STAGE" "$SCRIPT" >"$STAGE/reinstall.out" 2>&1 &&
   grep -q 'preserve me' "$STAGE/var/log/tnt-health.log"; then
    pass "reinstallation is idempotent and preserves logs"
else
    fail_case "reinstallation is idempotent and preserves logs"
fi

if DESTDIR=/ "$SCRIPT" >"$STAGE/root-dest.out" 2>&1; then
    fail_case "DESTDIR slash is rejected"
else
    pass "DESTDIR slash cannot bypass live-install checks"
fi

echo
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
