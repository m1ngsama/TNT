#!/bin/sh
# Contract tests for the dependency-free performance benchmark driver.

PASS=0
FAIL=0
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
SCRIPT="$SCRIPT_DIR/../scripts/perf_benchmark.py"
SOAK_SCRIPT="$SCRIPT_DIR/../scripts/perf_soak.py"

pass() {
    echo "✓ $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "✗ $1"
    FAIL=$((FAIL + 1))
}

echo "=== TNT Performance Benchmark Contract Tests ==="

if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 not installed; skipping performance benchmark contract tests"
    exit 0
fi

if SELF_TEST_OUTPUT=$("$SCRIPT" --self-test 2>&1) &&
   printf '%s\n' "$SELF_TEST_OUTPUT" | grep -q 'self-test passed'; then
    pass "percentile and budget helpers pass self-test"
else
    fail "benchmark helper self-test"
fi

if SOAK_SELF_TEST_OUTPUT=$("$SOAK_SCRIPT" --self-test 2>&1) &&
   printf '%s\n' "$SOAK_SELF_TEST_OUTPUT" | grep -q 'self-test passed'; then
    pass "durability benchmark helpers pass self-test"
else
    fail "durability benchmark helper self-test"
fi

if SOAK_HELP_OUTPUT=$("$SOAK_SCRIPT" --help 2>&1) &&
   printf '%s\n' "$SOAK_HELP_OUTPUT" | grep -q -- '--duration' &&
   printf '%s\n' "$SOAK_HELP_OUTPUT" | grep -q -- '--server-wrapper'; then
    pass "durability benchmark exposes duration and target-host controls"
else
    fail "durability benchmark help surface"
fi

if HELP_OUTPUT=$("$SCRIPT" --help 2>&1) &&
   printf '%s\n' "$HELP_OUTPUT" | grep -q -- '--enforce' &&
   printf '%s\n' "$HELP_OUTPUT" | grep -q -- '--history-records' &&
   printf '%s\n' "$HELP_OUTPUT" | grep -q -- '--storm-clients' &&
   printf '%s\n' "$HELP_OUTPUT" | grep -q -- '--module-paths' &&
   printf '%s\n' "$HELP_OUTPUT" | grep -q -- '--slow-client-characters'; then
    pass "benchmark exposes budget and full-scenario controls"
else
    fail "benchmark help surface"
fi

BAD_WRAPPER_OUTPUT=$("$SCRIPT" --server-wrapper tnt-missing-wrapper 2>&1)
BAD_WRAPPER_STATUS=$?
if [ "$BAD_WRAPPER_STATUS" -ne 0 ] &&
   printf '%s\n' "$BAD_WRAPPER_OUTPUT" | grep -q 'wrapper executable not found'; then
    pass "benchmark rejects unavailable target-host wrappers"
else
    fail "server wrapper validation"
fi

BAD_OUTPUT=$("$SCRIPT" --startup-samples 4 --handshake-samples 5 \
    --fanout-samples 5 --storm-clients 5 --slow-client-samples 5 2>&1)
BAD_STATUS=$?
if [ "$BAD_STATUS" -ne 0 ] &&
   printf '%s\n' "$BAD_OUTPUT" | grep -q 'at least 5 samples'; then
    pass "benchmark rejects statistically invalid sample counts"
else
    fail "minimum sample validation"
fi

BAD_MODULE_OUTPUT=$("$SCRIPT" --module-paths "$SCRIPT_DIR/missing-module" 2>&1)
BAD_MODULE_STATUS=$?
if [ "$BAD_MODULE_STATUS" -ne 0 ] &&
   printf '%s\n' "$BAD_MODULE_OUTPUT" | grep -q 'invalid module path'; then
    pass "benchmark rejects invalid module comparison input"
else
    fail "module path validation"
fi

ALL_OUTPUT=$("$SCRIPT" --enforce all --clients 8 2>&1)
ALL_STATUS=$?
if [ "$ALL_STATUS" -ne 0 ] &&
   printf '%s\n' "$ALL_OUTPUT" | grep -q 'requires exactly --clients 64'; then
    pass "full budget gate requires the target session count"
else
    fail "full budget session validation"
fi

DIAGNOSTIC=$(mktemp "${TMPDIR:-/tmp}/tnt-perf-diagnostic.XXXXXX")
DIAG_OUTPUT=$(
    "$SCRIPT" --binary "$DIAGNOSTIC.missing" --output "$DIAGNOSTIC" \
        --startup-samples 5 --handshake-samples 5 --fanout-samples 5 \
        --storm-clients 5 --clients 2 --messages 1 --history-records 1 \
        --slow-client-samples 5 2>&1
)
DIAG_STATUS=$?
if [ "$DIAG_STATUS" -ne 0 ] && [ -s "$DIAGNOSTIC" ] &&
   python3 - "$DIAGNOSTIC" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    report = json.load(handle)
assert report["status"] == "error"
assert report["schema_version"] == 3
assert report["metrics_complete"] is False
assert report["error"]["type"] == "BenchmarkError"
PY
then
    pass "benchmark failure preserves structured diagnostic JSON"
else
    printf '%s\n' "$DIAG_OUTPUT"
    fail "benchmark failure diagnostic report"
fi
rm -f "$DIAGNOSTIC"

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
