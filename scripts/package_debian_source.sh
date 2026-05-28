#!/bin/sh
# Assemble a Debian/Ubuntu source-package tree. This script never uploads.

set -eu

usage() {
    cat <<'USAGE'
Usage: scripts/package_debian_source.sh [--build] [OUT_DIR]

Create OUT_DIR/tnt-chat-$TNT_VERSION from tracked source files and copy the
draft Debian metadata to OUT_DIR/tnt-chat-$TNT_VERSION/debian.

Options:
  --build   run dpkg-buildpackage -S -us -uc after assembly

Default OUT_DIR: dist/debian-source
USAGE
}

fail() {
    echo "package-debian-source: $*" >&2
    exit 1
}

BUILD=0
OUT_DIR=${TNT_DEBIAN_SOURCE_OUT:-dist/debian-source}
OUT_SET=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build)
            BUILD=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        -*)
            fail "unknown option: $1"
            ;;
        *)
            [ "$OUT_SET" -eq 0 ] || fail "multiple output directories"
            OUT_DIR=$1
            OUT_SET=1
            ;;
    esac
    shift
done

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

VERSION=$(sed -n 's/^#define TNT_VERSION "\([^"]*\)".*/\1/p' include/common.h)
[ -n "$VERSION" ] || fail "could not read TNT_VERSION"

SOURCE_NAME="tnt-chat-$VERSION"
SOURCE_ROOT="$OUT_DIR/$SOURCE_NAME"

[ ! -e "$SOURCE_ROOT" ] || fail "$SOURCE_ROOT already exists"
mkdir -p "$OUT_DIR"
mkdir -p "$SOURCE_ROOT"

git ls-files -z | cpio -0 -pdm "$SOURCE_ROOT" >/dev/null 2>&1
cp -R "$ROOT/packaging/debian/debian" "$SOURCE_ROOT/debian"

[ -f "$SOURCE_ROOT/debian/control" ] || fail "missing debian/control"
[ -x "$SOURCE_ROOT/debian/rules" ] || fail "missing executable debian/rules"
[ -x "$SOURCE_ROOT/debian/postinst" ] || fail "missing executable debian/postinst"

echo "Debian source tree assembled: $SOURCE_ROOT"

if [ "$BUILD" -eq 1 ]; then
    command -v dpkg-buildpackage >/dev/null 2>&1 ||
        fail "dpkg-buildpackage not found"
    (
        cd "$SOURCE_ROOT"
        dpkg-buildpackage -S -us -uc
    )
fi
