#!/bin/sh
# Verify that a release tag matches TNT_VERSION.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

fail() {
    echo "release-ref-check: $*" >&2
    exit 1
}

ref=${1:-${GITHUB_REF_NAME:-}}
[ -n "$ref" ] || fail "missing release ref; pass vX.Y.Z or set GITHUB_REF_NAME"

case "$ref" in
    refs/tags/*) tag=${ref#refs/tags/} ;;
    *) tag=$ref ;;
esac

printf '%s\n' "$tag" | grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$' ||
    fail "release ref must be vMAJOR.MINOR.PATCH, got $tag"

version=$(sed -n 's/^#define TNT_VERSION "\([^"]*\)".*/\1/p' include/common.h)
[ -n "$version" ] || fail "could not read TNT_VERSION from include/common.h"

[ "$tag" = "v$version" ] ||
    fail "release tag $tag does not match TNT_VERSION $version"

echo "release ref matches TNT_VERSION: $tag"
