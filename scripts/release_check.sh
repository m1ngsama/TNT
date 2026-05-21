#!/bin/sh
# Local release preflight. This never tags, pushes, publishes, or deploys.

set -eu

STRICT=0

usage() {
    cat <<'USAGE'
Usage: scripts/release_check.sh [--strict]

Default checks:
  - version metadata alignment
  - clean build
  - unit tests
  - staged install layout with PREFIX=/usr and DESTDIR
  - Arch/Homebrew packaging syntax

Environment:
  RUN_INTEGRATION=1  also run full make test
  PORT=12720         base port for integration tests

Strict checks additionally require real package checksums and a local vX.Y.Z tag.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --strict)
            STRICT=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

fail() {
    echo "release-check: $*" >&2
    exit 1
}

step() {
    printf '\n==> %s\n' "$*"
}

version=$(sed -n 's/^#define TNT_VERSION "\([^"]*\)".*/\1/p' include/common.h)
[ -n "$version" ] || fail "could not read TNT_VERSION from include/common.h"

step "checking version metadata for $version"
grep -q "\"TNT $version\"" tnt.1 ||
    fail "tnt.1 does not mention TNT $version"
grep -q "^pkgver=$version$" packaging/arch/PKGBUILD ||
    fail "packaging/arch/PKGBUILD pkgver does not match $version"
grep -q "v${version}.tar.gz" packaging/homebrew/tnt-chat.rb ||
    fail "packaging/homebrew/tnt-chat.rb URL does not match v$version"

step "building"
make clean
make

actual_version=$(./tnt --version)
[ "$actual_version" = "tnt $version" ] ||
    fail "binary version mismatch: expected 'tnt $version', got '$actual_version'"

step "running unit tests"
make -C tests/unit clean
make -C tests/unit run

if [ "${RUN_INTEGRATION:-0}" = "1" ]; then
    step "running full integration tests"
    make test PORT="${PORT:-12720}"
fi

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/tnt-release-check.XXXXXX")
cleanup() {
    rm -rf "$tmpdir"
}
trap cleanup EXIT INT TERM

step "checking staged install layout"
make DESTDIR="$tmpdir" PREFIX=/usr install
make DESTDIR="$tmpdir" PREFIX=/usr install-systemd

[ -x "$tmpdir/usr/bin/tnt" ] || fail "missing executable: /usr/bin/tnt"
[ -f "$tmpdir/usr/share/man/man1/tnt.1" ] || fail "missing manpage: /usr/share/man/man1/tnt.1"
[ -f "$tmpdir/usr/lib/systemd/system/tnt.service" ] ||
    fail "missing systemd unit: /usr/lib/systemd/system/tnt.service"

step "checking packaging syntax"
if command -v bash >/dev/null 2>&1; then
    bash -n packaging/arch/PKGBUILD
else
    echo "bash not found; skipping PKGBUILD syntax check"
fi

if command -v ruby >/dev/null 2>&1; then
    ruby -c packaging/homebrew/tnt-chat.rb
else
    echo "ruby not found; skipping Homebrew formula syntax check"
fi

if [ "$STRICT" -eq 1 ]; then
    step "checking strict release gates"
    ! grep -q "sha256sums=('SKIP')" packaging/arch/PKGBUILD ||
        fail "replace PKGBUILD sha256sums before strict release"
    ! grep -q "REPLACE_WITH_RELEASE_TARBALL_SHA256" packaging/homebrew/tnt-chat.rb ||
        fail "replace Homebrew sha256 before strict release"
    git rev-parse -q --verify "refs/tags/v$version" >/dev/null ||
        fail "missing local tag v$version"
fi

step "release preflight passed"
