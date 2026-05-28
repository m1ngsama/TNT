#!/bin/sh
# Release-artifact gate regression tests.

set -u

PASS=0
FAIL=0
SCRIPT="../scripts/package_release_assets.sh"
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-release-artifact-test.XXXXXX")

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

write_elf_x86_64() {
    printf '\177ELF\002\001\001\000\000\000\000\000\000\000\000\000\002\000\076\000\001\000\000\000' > "$1"
}

write_elf_aarch64() {
    printf '\177ELF\002\001\001\000\000\000\000\000\000\000\000\000\002\000\267\000\001\000\000\000' > "$1"
}

write_macho_x86_64() {
    printf '\317\372\355\376\007\000\000\001\003\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000' > "$1"
}

write_macho_arm64() {
    printf '\317\372\355\376\014\000\000\001\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000' > "$1"
}

build_artifact_tree() {
    artifact_root=$1
    include_source=$2
    ver=$(version)

    mkdir -p \
        "$artifact_root/linux-amd64" \
        "$artifact_root/linux-arm64" \
        "$artifact_root/darwin-amd64" \
        "$artifact_root/darwin-arm64"

    write_elf_x86_64 "$artifact_root/linux-amd64/tnt-linux-amd64"
    write_elf_x86_64 "$artifact_root/linux-amd64/tntctl-linux-amd64"
    write_elf_aarch64 "$artifact_root/linux-arm64/tnt-linux-arm64"
    write_elf_aarch64 "$artifact_root/linux-arm64/tntctl-linux-arm64"
    write_macho_x86_64 "$artifact_root/darwin-amd64/tnt-darwin-amd64"
    write_macho_x86_64 "$artifact_root/darwin-amd64/tntctl-darwin-amd64"
    write_macho_arm64 "$artifact_root/darwin-arm64/tnt-darwin-arm64"
    write_macho_arm64 "$artifact_root/darwin-arm64/tntctl-darwin-arm64"

    if [ "$include_source" = "yes" ]; then
        mkdir -p "$artifact_root/source"
        ../scripts/package_source_archive.sh HEAD "$artifact_root/source" >/dev/null
    fi
}

expect_file() {
    path=$1
    name=$2
    [ -f "$path" ] && pass "$name" || fail "$name missing"
}

echo "=== TNT Release Artifact Gate Tests ==="

if [ ! -x "$SCRIPT" ]; then
    echo "Error: script $SCRIPT not found or not executable."
    exit 1
fi

VER=$(version)
ARTIFACT_ROOT="$STATE_DIR/artifacts"
OUT_DIR="$STATE_DIR/out"
build_artifact_tree "$ARTIFACT_ROOT" yes

OUTPUT=$("$SCRIPT" "$ARTIFACT_ROOT" "$OUT_DIR" 2>&1)
STATUS=$?
if [ "$STATUS" -eq 0 ] &&
   printf '%s\n' "$OUTPUT" | grep -q 'release artifact bundle ready'; then
    pass "complete artifact set is accepted"
else
    fail "complete artifact set failed" "$OUTPUT"
fi

for asset in \
    tnt-linux-amd64 \
    tntctl-linux-amd64 \
    tnt-linux-arm64 \
    tntctl-linux-arm64 \
    tnt-darwin-amd64 \
    tntctl-darwin-amd64 \
    tnt-darwin-arm64 \
    tntctl-darwin-arm64 \
    "tnt-chat-v$VER-source.tar.gz" \
    checksums.txt
do
    expect_file "$OUT_DIR/$asset" "bundles $asset"
done

if grep -q "  tnt-linux-amd64$" "$OUT_DIR/checksums.txt" &&
   grep -q "  tnt-chat-v$VER-source.tar.gz$" "$OUT_DIR/checksums.txt"; then
    pass "checksums include binaries and source archive"
else
    fail "checksums are incomplete" "$(cat "$OUT_DIR/checksums.txt" 2>/dev/null)"
fi

DUP_ROOT="$STATE_DIR/duplicate"
DUP_OUT="$STATE_DIR/duplicate-out"
build_artifact_tree "$DUP_ROOT" yes
mkdir -p "$DUP_ROOT/duplicate"
cp "$DUP_ROOT/linux-amd64/tnt-linux-amd64" "$DUP_ROOT/duplicate/tnt-linux-amd64"
DUP_OUTPUT=$("$SCRIPT" "$DUP_ROOT" "$DUP_OUT" 2>&1)
DUP_STATUS=$?
if [ "$DUP_STATUS" -ne 0 ] &&
   printf '%s\n' "$DUP_OUTPUT" | grep -q 'expected exactly one artifact named tnt-linux-amd64'; then
    pass "duplicate artifact is rejected"
else
    fail "duplicate artifact handling" "$DUP_OUTPUT"
fi

MISSING_ROOT="$STATE_DIR/missing"
MISSING_OUT="$STATE_DIR/missing-out"
build_artifact_tree "$MISSING_ROOT" no
MISSING_OUTPUT=$("$SCRIPT" "$MISSING_ROOT" "$MISSING_OUT" 2>&1)
MISSING_STATUS=$?
if [ "$MISSING_STATUS" -ne 0 ] &&
   printf '%s\n' "$MISSING_OUTPUT" | grep -q "expected exactly one artifact named tnt-chat-v$VER-source.tar.gz"; then
    pass "missing source archive is rejected"
else
    fail "missing source archive handling" "$MISSING_OUTPUT"
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
