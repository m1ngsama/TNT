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
  - script tests
  - staged install layout with PREFIX=/usr and DESTDIR
  - installer shell syntax
  - Debian packaging metadata
  - Arch/Homebrew packaging syntax

Environment:
  RUN_INTEGRATION=1  also run full make test
  RUN_SOAK=1         also run the configurable soak test
  RUN_SLOW_CLIENT=1  also run the slow-client backpressure test
  PORT=12720         base port for integration tests

Strict checks additionally require a clean tree, a vX.Y.Z tag at HEAD, a
matching changelog release section, non-placeholder maintainer metadata, and a
build from the tagged source archive.  Run `make package-publish-check` after
the final GitHub source archive exists to verify package checksums.
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
grep -q "\"TNT $version\"" tntctl.1 ||
    fail "tntctl.1 does not mention TNT $version"
grep -q "^pkgver=$version$" packaging/arch/PKGBUILD ||
    fail "packaging/arch/PKGBUILD pkgver does not match $version"
grep -q "pkgver = $version" packaging/arch/.SRCINFO ||
    fail "packaging/arch/.SRCINFO pkgver does not match $version"
grep -q "^pkgname=tnt-chat$" packaging/arch/PKGBUILD ||
    fail "packaging/arch/PKGBUILD pkgname is not tnt-chat"
grep -q "^pkgname = tnt-chat$" packaging/arch/.SRCINFO ||
    fail "packaging/arch/.SRCINFO pkgname is not tnt-chat"
grep -q "v${version}.tar.gz" packaging/homebrew/tnt-chat.rb ||
    fail "packaging/homebrew/tnt-chat.rb URL does not match v$version"
grep -q "^class TntChat < Formula$" packaging/homebrew/tnt-chat.rb ||
    fail "packaging/homebrew/tnt-chat.rb formula class is not TntChat"
grep -q 'depends_on "libssh"' packaging/homebrew/tnt-chat.rb ||
    fail "packaging/homebrew/tnt-chat.rb must depend on libssh"
grep -q "^tnt-chat (${version}-1)" packaging/debian/debian/changelog ||
    fail "packaging/debian/debian/changelog version does not match $version"
grep -q "^Source: tnt-chat$" packaging/debian/debian/control ||
    fail "packaging/debian/debian/control Source is not tnt-chat"

step "building"
make clean
make

actual_version=$(./tnt --version)
[ "$actual_version" = "tnt $version" ] ||
    fail "binary version mismatch: expected 'tnt $version', got '$actual_version'"
tntctl_version=$(./tntctl --version)
[ "$tntctl_version" = "tntctl $version" ] ||
    fail "control binary version mismatch: expected 'tntctl $version', got '$tntctl_version'"

step "running unit tests"
make -C tests/unit clean
make -C tests/unit run

step "running script tests"
make script-test

step "checking client I/O ownership boundaries"
! grep -R "client_send(target" src include >/dev/null ||
    fail "cross-client target writes must be queued through client_queue_bell"
! grep -R "client_send(targets" src include >/dev/null ||
    fail "cross-client target-array writes must be queued through client_queue_bell"
! grep -n "pthread_mutex_lock(&.*->io_lock)" src/commands.c >/dev/null ||
    fail "commands.c must not use SSH io_lock for in-memory command state"
! grep -n "client_addref(client)" src/bootstrap.c >/dev/null ||
    fail "bootstrap.c must let client_install_channel_callbacks own callback refs"
grep -q "client_release_session(client)" src/input.c ||
    fail "input.c must release session ownership through client_release_session"
if grep -R "ssh_channel_write" src include | grep -v "^src/client.c:" >/dev/null; then
    fail "raw SSH channel writes must stay inside src/client.c"
fi

if [ "${RUN_INTEGRATION:-0}" = "1" ]; then
    step "running full integration tests"
    make test PORT="${PORT:-12720}"
fi

if [ "${RUN_SOAK:-0}" = "1" ]; then
    step "running soak test"
    make soak-test PORT="$((${PORT:-12720} + 30))" \
        DURATION="${SOAK_DURATION:-8}" RECONNECTS="${SOAK_RECONNECTS:-5}"
fi

if [ "${RUN_SLOW_CLIENT:-0}" = "1" ]; then
    step "running slow-client test"
    make slow-client-test PORT="$((${PORT:-12720} + 40))" \
        DURATION="${SLOW_CLIENT_DURATION:-8}" \
        BURST_CHARS="${SLOW_CLIENT_BURST_CHARS:-1600}"
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
[ -x "$tmpdir/usr/bin/tntctl" ] || fail "missing executable: /usr/bin/tntctl"
[ -f "$tmpdir/usr/share/man/man1/tnt.1" ] || fail "missing manpage: /usr/share/man/man1/tnt.1"
[ -f "$tmpdir/usr/share/man/man1/tntctl.1" ] || fail "missing manpage: /usr/share/man/man1/tntctl.1"
[ -f "$tmpdir/usr/lib/systemd/system/tnt.service" ] ||
    fail "missing systemd unit: /usr/lib/systemd/system/tnt.service"
grep -q "^ExecStart=/usr/bin/tnt$" "$tmpdir/usr/lib/systemd/system/tnt.service" ||
    fail "systemd unit ExecStart does not match PREFIX=/usr install path"

step "checking installed log maintenance modes"
log_smoke="$tmpdir/messages.log"
recovered_log="$tmpdir/recovered.messages.log"
recover_report="$tmpdir/recovered.report"
smoke_ts=$(date -u +%Y-%m-%dT%H:%M:%SZ)
cat > "$log_smoke" <<EOF
$smoke_ts|alice|one
$smoke_ts|mallory|extra|pipe
$smoke_ts|bob|two
EOF
if "$tmpdir/usr/bin/tnt" --log-check "$log_smoke" >"$tmpdir/log-check.out" 2>&1; then
    fail "installed tnt --log-check should report invalid records"
fi
grep -q '^valid_records 2$' "$tmpdir/log-check.out" ||
    fail "installed tnt --log-check did not report valid records"
grep -q '^invalid_records 1$' "$tmpdir/log-check.out" ||
    fail "installed tnt --log-check did not report invalid records"
if "$tmpdir/usr/bin/tnt" --log-recover "$log_smoke" \
        >"$recovered_log" 2>"$recover_report"; then
    fail "installed tnt --log-recover should report invalid records"
fi
grep -q "$smoke_ts|alice|one" "$recovered_log" ||
    fail "installed tnt --log-recover missed alice record"
grep -q "$smoke_ts|bob|two" "$recovered_log" ||
    fail "installed tnt --log-recover missed bob record"
! grep -q 'mallory' "$recovered_log" ||
    fail "installed tnt --log-recover preserved invalid record"
grep -q '^invalid_records 1$' "$recover_report" ||
    fail "installed tnt --log-recover did not report invalid records"

step "checking installer syntax"
sh -n install.sh
sh -n scripts/check_release_ref.sh
sh -n scripts/package_publish_check.sh
scripts/check_release_ref.sh "v$version"
bad_ref=v0.0.0
[ "$version" != "0.0.0" ] || bad_ref=v9.9.9
if scripts/check_release_ref.sh "$bad_ref" >/dev/null 2>&1; then
    fail "release ref check accepted a mismatched tag"
fi

step "checking Debian packaging metadata"
[ -x packaging/debian/debian/rules ] ||
    fail "packaging/debian/debian/rules must be executable"
[ -x packaging/debian/debian/postinst ] ||
    fail "packaging/debian/debian/postinst must be executable"
grep -q "^3.0 (quilt)$" packaging/debian/debian/source/format ||
    fail "unsupported Debian source format"
grep -q "adduser .* tnt" packaging/debian/debian/postinst ||
    fail "Debian postinst must create the tnt system user"
grep -q " adduser" packaging/debian/debian/control ||
    fail "Debian package must depend on adduser for postinst user creation"

step "checking Debian source assembly"
sh -n scripts/package_debian_source.sh
scripts/package_debian_source.sh "$tmpdir/debian-source"
[ -f "$tmpdir/debian-source/tnt-chat-$version/debian/control" ] ||
    fail "assembled Debian source tree is missing debian/control"
[ -x "$tmpdir/debian-source/tnt-chat-$version/debian/rules" ] ||
    fail "assembled Debian source tree is missing executable debian/rules"
[ -x "$tmpdir/debian-source/tnt-chat-$version/debian/postinst" ] ||
    fail "assembled Debian source tree is missing executable debian/postinst"

step "checking packaged system user metadata"
grep -q '^u tnt ' packaging/arch/tnt-chat.sysusers ||
    fail "Arch sysusers file must create the tnt system user"
grep -q 'usr/lib/sysusers.d' packaging/arch/PKGBUILD ||
    fail "PKGBUILD must install the sysusers.d file"

step "checking Homebrew service metadata"
grep -q "service do" packaging/homebrew/tnt-chat.rb ||
    fail "Homebrew formula must define a brew services entry"
grep -q 'opt_bin/"tnt"' packaging/homebrew/tnt-chat.rb ||
    fail "Homebrew service must run the installed tnt binary"

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
    [ -z "$(git status --short)" ] ||
        fail "working tree must be clean for strict release"
    git rev-parse -q --verify "refs/tags/v$version" >/dev/null ||
        fail "missing local tag v$version"
    [ "$(git rev-parse "refs/tags/v$version^{}")" = "$(git rev-parse HEAD)" ] ||
        fail "local tag v$version does not point at HEAD"
    grep -q "^## $version " docs/CHANGELOG.md ||
        fail "docs/CHANGELOG.md does not contain a release section for $version"
    ! grep -R "REPLACE_WITH_EMAIL" packaging/arch packaging/debian >/dev/null ||
        fail "replace maintainer email placeholders before strict release"

    step "checking tagged source archive"
    archive="$tmpdir/tnt-$version-source.tar.gz"
    archive_extract="$tmpdir/source"
    archive_install="$tmpdir/source-install"
    archive_root="$archive_extract/TNT-$version"

    git archive --format=tar.gz --prefix="TNT-$version/" \
        -o "$archive" "refs/tags/v$version"
    mkdir -p "$archive_extract"
    tar -xzf "$archive" -C "$archive_extract"

    [ -f "$archive_root/src/tntctl.c" ] ||
        fail "tagged source archive is missing src/tntctl.c"
    [ -f "$archive_root/tnt.1" ] ||
        fail "tagged source archive is missing tnt.1"
    [ -f "$archive_root/tntctl.1" ] ||
        fail "tagged source archive is missing tntctl.1"
    [ -f "$archive_root/LICENSE" ] ||
        fail "tagged source archive is missing LICENSE"

    (
        cd "$archive_root"
        make
        make DESTDIR="$archive_install" PREFIX=/usr install
        make DESTDIR="$archive_install" PREFIX=/usr install-systemd
    )

    [ -x "$archive_install/usr/bin/tnt" ] ||
        fail "tagged source install is missing /usr/bin/tnt"
    [ -x "$archive_install/usr/bin/tntctl" ] ||
        fail "tagged source install is missing /usr/bin/tntctl"
    [ -f "$archive_install/usr/share/man/man1/tnt.1" ] ||
        fail "tagged source install is missing tnt.1"
    [ -f "$archive_install/usr/share/man/man1/tntctl.1" ] ||
        fail "tagged source install is missing tntctl.1"
    grep -q "^ExecStart=/usr/bin/tnt$" \
        "$archive_install/usr/lib/systemd/system/tnt.service" ||
        fail "tagged source systemd unit ExecStart does not match /usr/bin/tnt"
fi

step "release preflight passed"
