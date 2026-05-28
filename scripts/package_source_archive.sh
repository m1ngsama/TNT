#!/bin/sh
# Build and validate the explicit release source archive.

set -eu

usage() {
    cat <<'USAGE'
Usage: scripts/package_source_archive.sh REF [OUT_DIR]

REF is a git ref, commit, or release tag to archive. OUT_DIR defaults to dist.
The archive name is tnt-chat-v$TNT_VERSION-source.tar.gz, and its top-level
directory is TNT-$TNT_VERSION/.

When REF is a SemVer tag such as v1.2.3 or refs/tags/v1.2.3, the tag must match
TNT_VERSION from that ref. This script only builds and validates the archive; it
does not tag, publish, upload, or deploy.
USAGE
}

fail() {
    echo "source-archive: $*" >&2
    exit 1
}

require_archive_entry() {
    entry=$1
    label=$2

    printf '%s\n' "$archive_listing" |
        awk -v target="$entry" '$0 == target { found = 1 } END { exit found ? 0 : 1 }' ||
        fail "source archive is missing $label"
}

[ "${1:-}" != "-h" ] && [ "${1:-}" != "--help" ] || {
    usage
    exit 0
}

REF=${1:-}
OUT_DIR=${2:-dist}

[ -n "$REF" ] || {
    usage >&2
    exit 2
}

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

commit=$(git rev-parse --verify "$REF^{commit}") ||
    fail "could not resolve git ref: $REF"

case "$OUT_DIR" in
    /*) ;;
    *) OUT_DIR="$ROOT/$OUT_DIR" ;;
esac

version=$(git show "$commit:include/common.h" |
    sed -n 's/^#define TNT_VERSION "\([^"]*\)".*/\1/p')
[ -n "$version" ] || fail "could not read TNT_VERSION from $REF"

case "$REF" in
    refs/tags/v[0-9]*.[0-9]*.[0-9]*)
        tag=${REF#refs/tags/}
        [ "$tag" = "v$version" ] ||
            fail "release tag $tag does not match TNT_VERSION $version"
        ;;
    v[0-9]*.[0-9]*.[0-9]*)
        [ "$REF" = "v$version" ] ||
            fail "release tag $REF does not match TNT_VERSION $version"
        ;;
esac

archive="$OUT_DIR/tnt-chat-v$version-source.tar.gz"

mkdir -p "$OUT_DIR"
[ ! -e "$archive" ] || fail "output already exists: $archive"

git archive --format=tar.gz --prefix="TNT-$version/" -o "$archive" "$commit"

archive_listing=$(tar -tzf "$archive") ||
    fail "archive is not a readable tar.gz: $archive"
require_archive_entry "TNT-$version/LICENSE" "LICENSE"
require_archive_entry "TNT-$version/src/tntctl.c" "src/tntctl.c"
require_archive_entry "TNT-$version/packaging/README.md" "packaging/README.md"
require_archive_entry "TNT-$version/tnt.1" "tnt.1"
require_archive_entry "TNT-$version/tntctl.1" "tntctl.1"

echo "$archive"
