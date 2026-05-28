#!/bin/sh
# Verify package-manager recipes against a final release source archive.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

fail() {
    echo "package-publish-check: $*" >&2
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

require_archive_entry() {
    entry=$1
    label=$2

    printf '%s\n' "$source_listing" |
        awk -v target="$entry" '$0 == target { found = 1 } END { exit found ? 0 : 1 }' ||
        fail "SOURCE_TARBALL is missing $label"
}

version=$(sed -n 's/^#define TNT_VERSION "\([^"]*\)".*/\1/p' include/common.h)
[ -n "$version" ] || fail "could not read TNT_VERSION from include/common.h"
release_source="tnt-chat-v${version}-source.tar.gz"

source_tarball=${SOURCE_TARBALL:-${RELEASE_SOURCE_TARBALL:-}}
[ -n "$source_tarball" ] ||
    fail "set SOURCE_TARBALL to the explicit release source archive"
[ -f "$source_tarball" ] ||
    fail "SOURCE_TARBALL does not exist: $source_tarball"
source_listing=$(tar -tzf "$source_tarball") ||
    fail "SOURCE_TARBALL is not a readable tar.gz archive"
require_archive_entry "TNT-$version/LICENSE" "LICENSE"
require_archive_entry "TNT-$version/packaging/README.md" "packaging/README.md"
require_archive_entry "TNT-$version/src/tntctl.c" "src/tntctl.c"
require_archive_entry "TNT-$version/tnt.1" "tnt.1"
require_archive_entry "TNT-$version/tntctl.1" "tntctl.1"

! grep -R "REPLACE_WITH_EMAIL" packaging/arch packaging/debian >/dev/null ||
    fail "replace maintainer email placeholders before package publishing"

arch_sha=$(sed -n "s/^[[:space:]]*sha256sums=('\([^']*\)'.*/\1/p" \
    packaging/arch/PKGBUILD | head -n 1)
srcinfo_sha=$(sed -n 's/^[[:space:]]*sha256sums = \([^[:space:]]*\).*/\1/p' \
    packaging/arch/.SRCINFO | head -n 1)
brew_sha=$(sed -n 's/^[[:space:]]*sha256 "\([^"]*\)".*/\1/p' \
    packaging/homebrew/tnt-chat.rb | head -n 1)

[ -n "$arch_sha" ] || fail "could not read PKGBUILD source checksum"
[ -n "$srcinfo_sha" ] || fail "could not read .SRCINFO source checksum"
[ -n "$brew_sha" ] || fail "could not read Homebrew source checksum"
[ "$arch_sha" != "SKIP" ] || fail "replace PKGBUILD sha256sums before publishing"
[ "$srcinfo_sha" != "SKIP" ] || fail "replace .SRCINFO sha256sums before publishing"
[ "$brew_sha" != "REPLACE_WITH_RELEASE_TARBALL_SHA256" ] ||
    fail "replace Homebrew sha256 before publishing"

expected_sha=$(sha256_of "$source_tarball")
[ "$arch_sha" = "$expected_sha" ] ||
    fail "PKGBUILD source checksum does not match SOURCE_TARBALL"
[ "$srcinfo_sha" = "$expected_sha" ] ||
    fail ".SRCINFO source checksum does not match SOURCE_TARBALL"
[ "$brew_sha" = "$expected_sha" ] ||
    fail "Homebrew source checksum does not match SOURCE_TARBALL"

grep -q "^pkgver=$version$" packaging/arch/PKGBUILD ||
    fail "PKGBUILD pkgver does not match $version"
grep -q "pkgver = $version" packaging/arch/.SRCINFO ||
    fail ".SRCINFO pkgver does not match $version"
grep -q '${pkgname}-v${pkgver}-source.tar.gz' packaging/arch/PKGBUILD ||
    fail "PKGBUILD source must use the release source archive"
grep -q "$release_source" packaging/arch/.SRCINFO ||
    fail ".SRCINFO source does not match $release_source"
grep -q "$release_source" packaging/homebrew/tnt-chat.rb ||
    fail "Homebrew URL does not match $release_source"
grep -q "^tnt-chat (${version}-1)" packaging/debian/debian/changelog ||
    fail "Debian changelog version does not match $version"

echo "package recipes match SOURCE_TARBALL for $version: $expected_sha"
