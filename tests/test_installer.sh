#!/bin/sh

set -u

PASS=0
FAIL=0
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-installer-test.XXXXXX")

cleanup() {
    rm -rf "$STATE_DIR"
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

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

OS=$(uname -s | tr '[:upper:]' '[:lower:]')
case $(uname -m) in
    x86_64) ARCH=amd64 ;;
    aarch64|arm64) ARCH=arm64 ;;
    *) echo "unsupported test architecture" >&2; exit 1 ;;
esac
SERVER_BINARY="tnt-$OS-$ARCH"
CTL_BINARY="tntctl-$OS-$ARCH"

FAKE_CURL="$STATE_DIR/curl"
cat >"$FAKE_CURL" <<'EOF'
#!/bin/sh
set -eu
output=
url=
while [ "$#" -gt 0 ]; do
    case "$1" in
        -o) output=$2; shift 2 ;;
        -*) shift ;;
        *) url=$1; shift ;;
    esac
done
[ -n "$output" ] && [ -n "$url" ]
cp "$FIXTURE_ROOT/${url##*/}" "$output"
EOF
chmod +x "$FAKE_CURL"

make_binaries() {
    fixture=$1
    mkdir -p "$fixture"
    printf '#!/bin/sh\nexit 0\n' >"$fixture/$SERVER_BINARY"
    printf '#!/bin/sh\nexit 0\n' >"$fixture/$CTL_BINARY"
    chmod +x "$fixture/$SERVER_BINARY" "$fixture/$CTL_BINARY"
}

make_source() {
    fixture=$1
    version=$2
    layout=$3
    source_root="$fixture/tree/TNT-${version#v}"

    mkdir -p "$source_root"
    if [ "$layout" = standard ]; then
        mkdir -p "$source_root/packaging/completions"
        for file in tntctl.1 tnt-message-log.5 tnt-chat.7 tnt-exec.7 \
                tnt-module-protocol.7 tnt.8; do
            cp "$ROOT/$file" "$source_root/$file"
        done
        for file in tntctl.bash _tntctl tntctl.fish; do
            cp "$ROOT/packaging/completions/$file" \
                "$source_root/packaging/completions/$file"
        done
    else
        printf '.TH TNT 1 "2025-01-01" "TNT legacy"\n' \
            >"$source_root/tnt.1"
        printf '.TH TNTCTL 1 "2025-01-01" "TNT legacy"\n' \
            >"$source_root/tntctl.1"
    fi
    tar -czf "$fixture/tnt-chat-$version-source.tar.gz" \
        -C "$fixture/tree" "TNT-${version#v}"
}

make_checksums() {
    fixture=$1
    version=$2
    include_source=$3
    checksums="$fixture/checksums.txt"

    printf '%s  %s\n' "$(sha256_of "$fixture/$SERVER_BINARY")" \
        "$SERVER_BINARY" >"$checksums"
    printf '%s  %s\n' "$(sha256_of "$fixture/$CTL_BINARY")" \
        "$CTL_BINARY" >>"$checksums"
    if [ "$include_source" = yes ]; then
        source_asset="tnt-chat-$version-source.tar.gz"
        printf '%s  %s\n' "$(sha256_of "$fixture/$source_asset")" \
            "$source_asset" >>"$checksums"
    fi
}

run_installer() {
    fixture=$1
    prefix=$2
    version=$3
    FIXTURE_ROOT="$fixture" PREFIX="$prefix" VERSION="$version" \
        CURL="$FAKE_CURL" sh "$ROOT/install.sh"
}

STANDARD_FIXTURE="$STATE_DIR/standard"
STANDARD_PREFIX="$STATE_DIR/standard-prefix"
STANDARD_VERSION=v9.9.9
make_binaries "$STANDARD_FIXTURE"
make_source "$STANDARD_FIXTURE" "$STANDARD_VERSION" standard
make_checksums "$STANDARD_FIXTURE" "$STANDARD_VERSION" yes
if run_installer "$STANDARD_FIXTURE" "$STANDARD_PREFIX" \
        "$STANDARD_VERSION" >"$STATE_DIR/standard.out" 2>&1 &&
   [ -x "$STANDARD_PREFIX/bin/tnt" ] &&
   [ -x "$STANDARD_PREFIX/bin/tntctl" ] &&
   [ -f "$STANDARD_PREFIX/share/man/man1/tntctl.1" ] &&
   [ -f "$STANDARD_PREFIX/share/man/man5/tnt-message-log.5" ] &&
   [ -f "$STANDARD_PREFIX/share/man/man7/tnt-chat.7" ] &&
   [ -f "$STANDARD_PREFIX/share/man/man7/tnt-exec.7" ] &&
   [ -f "$STANDARD_PREFIX/share/man/man7/tnt-module-protocol.7" ] &&
   [ -f "$STANDARD_PREFIX/share/man/man8/tnt.8" ] &&
   [ -f "$STANDARD_PREFIX/share/bash-completion/completions/tntctl" ] &&
   [ -f "$STANDARD_PREFIX/share/zsh/site-functions/_tntctl" ] &&
   [ -f "$STANDARD_PREFIX/share/fish/vendor_completions.d/tntctl.fish" ]; then
    pass "current release installs binaries, manuals, and completions"
else
    fail_case "current release installs binaries, manuals, and completions"
    sed -n '1,120p' "$STATE_DIR/standard.out"
fi

LEGACY_FIXTURE="$STATE_DIR/legacy"
LEGACY_PREFIX="$STATE_DIR/legacy-prefix"
LEGACY_VERSION=v9.9.8
make_binaries "$LEGACY_FIXTURE"
make_source "$LEGACY_FIXTURE" "$LEGACY_VERSION" legacy
make_checksums "$LEGACY_FIXTURE" "$LEGACY_VERSION" yes
if run_installer "$LEGACY_FIXTURE" "$LEGACY_PREFIX" \
        "$LEGACY_VERSION" >"$STATE_DIR/legacy.out" 2>&1 &&
   [ -f "$LEGACY_PREFIX/share/man/man1/tnt.1" ] &&
   [ -f "$LEGACY_PREFIX/share/man/man1/tntctl.1" ]; then
    pass "legacy source layout remains installable"
else
    fail_case "legacy source layout remains installable"
    sed -n '1,120p' "$STATE_DIR/legacy.out"
fi

BINARY_FIXTURE="$STATE_DIR/binary-only"
BINARY_PREFIX="$STATE_DIR/binary-only-prefix"
BINARY_VERSION=v9.9.7
make_binaries "$BINARY_FIXTURE"
make_checksums "$BINARY_FIXTURE" "$BINARY_VERSION" no
if run_installer "$BINARY_FIXTURE" "$BINARY_PREFIX" \
        "$BINARY_VERSION" >"$STATE_DIR/binary-only.out" 2>&1 &&
   [ -x "$BINARY_PREFIX/bin/tnt" ] &&
   [ ! -e "$BINARY_PREFIX/share/man" ]; then
    pass "release without a source asset installs binaries only"
else
    fail_case "release without a source asset installs binaries only"
    sed -n '1,120p' "$STATE_DIR/binary-only.out"
fi

BAD_FIXTURE="$STATE_DIR/bad-checksum"
BAD_PREFIX="$STATE_DIR/bad-checksum-prefix"
BAD_VERSION=v9.9.6
make_binaries "$BAD_FIXTURE"
make_source "$BAD_FIXTURE" "$BAD_VERSION" standard
make_checksums "$BAD_FIXTURE" "$BAD_VERSION" yes
{
    printf '%064d  %s\n' 0 "$SERVER_BINARY"
    sed -n '2,$p' "$BAD_FIXTURE/checksums.txt"
} >"$BAD_FIXTURE/bad-checksums.txt"
mv "$BAD_FIXTURE/bad-checksums.txt" "$BAD_FIXTURE/checksums.txt"
if run_installer "$BAD_FIXTURE" "$BAD_PREFIX" \
        "$BAD_VERSION" >"$STATE_DIR/bad-checksum.out" 2>&1; then
    fail_case "checksum mismatch is rejected before installation"
elif [ ! -e "$BAD_PREFIX/bin/tnt" ]; then
    pass "checksum mismatch is rejected before installation"
else
    fail_case "checksum mismatch is rejected before installation"
fi

printf '\nPASSED: %d\nFAILED: %d\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
