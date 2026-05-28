#!/bin/sh
# Collect release workflow artifacts into one flat, checksum-verified bundle.

set -eu

usage() {
    cat <<'USAGE'
Usage: scripts/package_release_assets.sh ARTIFACT_ROOT [OUT_DIR]

ARTIFACT_ROOT is the directory produced by actions/download-artifact.
OUT_DIR defaults to dist/release-assets.

The script validates the expected release asset names, verifies binary
architecture labels, verifies the source archive shape, writes checksums.txt,
and verifies that checksums.txt matches the assembled bundle. It never
publishes a release.
USAGE
}

fail() {
    echo "release-artifact-gate: $*" >&2
    exit 1
}

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        fail "sha256sum or shasum is required"
    fi
}

find_unique() {
    name=$1
    matches=$(find "$ARTIFACT_ROOT" -type f -name "$name" -print)
    count=$(printf '%s\n' "$matches" | sed '/^$/d' | wc -l | tr -d ' ')
    [ "$count" = "1" ] ||
        fail "expected exactly one artifact named $name, found $count"
    printf '%s\n' "$matches"
}

require_file_label() {
    path=$1
    pattern=$2
    label=$(file "$path")
    printf '%s\n' "$label" | grep -E "$pattern" >/dev/null ||
        fail "unexpected file type for $(basename "$path"): $label"
}

archive_has_entry() {
    entry=$1

    printf '%s\n' "$archive_listing" |
        awk -v target="$entry" '$0 == target { found = 1 } END { exit found ? 0 : 1 }'
}

require_archive_entry() {
    entry=$1
    label=$2

    archive_has_entry "$entry" ||
        fail "source archive is missing $label"
}

verify_asset() {
    name=$1
    path=$2

    [ -s "$path" ] || fail "empty artifact: $name"

    case "$name" in
        tnt-linux-amd64|tntctl-linux-amd64)
            require_file_label "$path" 'ELF 64-bit.*x86-64'
            ;;
        tnt-linux-arm64|tntctl-linux-arm64)
            require_file_label "$path" 'ELF 64-bit.*(aarch64|ARM aarch64)'
            ;;
        tnt-darwin-amd64|tntctl-darwin-amd64)
            require_file_label "$path" 'Mach-O 64-bit.*x86_64'
            ;;
        tnt-darwin-arm64|tntctl-darwin-arm64)
            require_file_label "$path" 'Mach-O 64-bit.*arm64'
            ;;
        tnt-chat-v*-source.tar.gz)
            archive_listing=$(tar -tzf "$path") ||
                fail "source archive is not a readable tar.gz: $name"
            require_archive_entry "TNT-$VERSION/LICENSE" "LICENSE"
            require_archive_entry "TNT-$VERSION/src/tntctl.c" "src/tntctl.c"
            require_archive_entry "TNT-$VERSION/packaging/README.md" "packaging/README.md"
            require_archive_entry "TNT-$VERSION/tnt.1" "tnt.1"
            require_archive_entry "TNT-$VERSION/tntctl.1" "tntctl.1"
            ;;
        *)
            fail "unexpected release artifact: $name"
            ;;
    esac
}

[ "${1:-}" != "-h" ] && [ "${1:-}" != "--help" ] || {
    usage
    exit 0
}

ARTIFACT_ROOT=${1:-}
OUT_DIR=${2:-dist/release-assets}

[ -n "$ARTIFACT_ROOT" ] || {
    usage >&2
    exit 2
}
[ -d "$ARTIFACT_ROOT" ] || fail "ARTIFACT_ROOT does not exist: $ARTIFACT_ROOT"
ARTIFACT_ROOT=$(CDPATH= cd -- "$ARTIFACT_ROOT" && pwd)

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

case "$OUT_DIR" in
    /*) ;;
    *) OUT_DIR="$ROOT/$OUT_DIR" ;;
esac

VERSION=$(sed -n 's/^#define TNT_VERSION "\([^"]*\)".*/\1/p' include/common.h)
[ -n "$VERSION" ] || fail "could not read TNT_VERSION"

SOURCE_ASSET="tnt-chat-v$VERSION-source.tar.gz"
EXPECTED_ASSETS="
tnt-linux-amd64
tntctl-linux-amd64
tnt-linux-arm64
tntctl-linux-arm64
tnt-darwin-amd64
tntctl-darwin-amd64
tnt-darwin-arm64
tntctl-darwin-arm64
$SOURCE_ASSET
"

mkdir -p "$OUT_DIR"

for name in $EXPECTED_ASSETS; do
    dst="$OUT_DIR/$name"
    [ ! -e "$dst" ] || fail "output already exists: $dst"
    src=$(find_unique "$name")
    verify_asset "$name" "$src"
    cp "$src" "$dst"
done

(
    cd "$OUT_DIR"
    : > checksums.txt
    for name in $EXPECTED_ASSETS; do
        printf '%s  %s\n' "$(sha256_of "$name")" "$name" >> checksums.txt
    done

    while read -r expected name; do
        [ -n "$expected" ] || continue
        actual=$(sha256_of "$name")
        [ "$actual" = "$expected" ] ||
            fail "checksum mismatch for $name"
    done < checksums.txt
)

echo "release artifact bundle ready: $OUT_DIR"
