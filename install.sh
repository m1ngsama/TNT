#!/bin/sh
# TNT installer
# Usage: curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh

set -e

VERSION=${VERSION:-latest}
if [ -n "${PREFIX:-}" ]; then
    INSTALL_PREFIX=$PREFIX
elif [ -n "${INSTALL_DIR:-}" ]; then
    case "$INSTALL_DIR" in
        */bin) INSTALL_PREFIX=${INSTALL_DIR%/bin} ;;
        *) INSTALL_PREFIX=$(dirname "$INSTALL_DIR") ;;
    esac
else
    INSTALL_PREFIX=/usr/local
fi
INSTALL_DIR=${INSTALL_DIR:-$INSTALL_PREFIX/bin}
SHARE_DIR=$INSTALL_PREFIX/share
REPO="m1ngsama/TNT"
CURL=${CURL:-curl}

fail() {
    echo "ERROR: $*" >&2
    exit 1
}

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || fail "$1 is required"
}

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        return 1
    fi
}

warn_missing_libssh() {
    case "$OS" in
        linux)
            if command -v ldconfig >/dev/null 2>&1 &&
               ldconfig -p 2>/dev/null | grep -q 'libssh\.so'; then
                return
            fi
            for path in /usr/lib/libssh.so* /usr/lib64/libssh.so* \
                        /lib/libssh.so* /lib64/libssh.so*; do
                [ -e "$path" ] && return
            done
            echo "WARNING: TNT requires the libssh runtime library."
            echo "Install it first, for example:"
            echo "  Ubuntu/Debian: sudo apt install libssh-4"
            echo "  Arch:          sudo pacman -S libssh"
            ;;
        darwin)
            if [ -e /opt/homebrew/opt/libssh/lib/libssh.dylib ] ||
               [ -e /usr/local/opt/libssh/lib/libssh.dylib ]; then
                return
            fi
            echo "WARNING: TNT requires the libssh runtime library."
            echo "Install it first:"
            echo "  brew install libssh"
            ;;
    esac
}

warn_missing_ssh() {
    command -v ssh >/dev/null 2>&1 && return
    echo "WARNING: tntctl requires the OpenSSH client (ssh)."
}

need_cmd "$CURL"
need_cmd awk
need_cmd install
need_cmd tar

# Detect OS and architecture
OS=$(uname -s | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)

case "$OS" in
    linux|darwin) ;;
    *) fail "Unsupported OS: $OS" ;;
esac

case "$ARCH" in
    x86_64) ARCH="amd64" ;;
    aarch64|arm64) ARCH="arm64" ;;
    *) fail "Unsupported architecture: $ARCH" ;;
esac

SERVER_BINARY="tnt-${OS}-${ARCH}"
CTL_BINARY="tntctl-${OS}-${ARCH}"

echo "=== TNT Installer ==="
echo "OS: $OS"
echo "Arch: $ARCH"
echo "Version: $VERSION"
echo ""
warn_missing_libssh
warn_missing_ssh

# Get latest version if not specified
if [ "$VERSION" = "latest" ]; then
    echo "Fetching latest version..."
    VERSION=$("$CURL" -fsSL "https://api.github.com/repos/$REPO/releases/latest" |
        sed -n 's/.*"tag_name":[[:space:]]*"\([^"]*\)".*/\1/p' |
        head -n 1)
    [ -n "$VERSION" ] || fail "Could not determine latest release version"
fi

echo "Installing version: $VERSION"

# Download
SERVER_URL="https://github.com/$REPO/releases/download/$VERSION/$SERVER_BINARY"
CTL_URL="https://github.com/$REPO/releases/download/$VERSION/$CTL_BINARY"
SOURCE_ASSET="tnt-chat-$VERSION-source.tar.gz"
SOURCE_URL="https://github.com/$REPO/releases/download/$VERSION/$SOURCE_ASSET"
CHECKSUM_URL="https://github.com/$REPO/releases/download/$VERSION/checksums.txt"
echo "Downloading from: $SERVER_URL"

SERVER_TMP_FILE=$(mktemp "${TMPDIR:-/tmp}/tnt.XXXXXX")
CTL_TMP_FILE=$(mktemp "${TMPDIR:-/tmp}/tntctl.XXXXXX")
SOURCE_TMP_FILE=$(mktemp "${TMPDIR:-/tmp}/tnt-source.XXXXXX")
SOURCE_TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-source-tree.XXXXXX")
CHECKSUM_FILE=$(mktemp "${TMPDIR:-/tmp}/tnt-checksums.XXXXXX")
INSTALL_CTL=0
cleanup() {
    rm -f "$SERVER_TMP_FILE" "$CTL_TMP_FILE" "$SOURCE_TMP_FILE"
    rm -f "$CHECKSUM_FILE"
    rm -rf "$SOURCE_TMP_DIR"
}
trap cleanup EXIT INT TERM

"$CURL" -fsSL -o "$SERVER_TMP_FILE" "$SERVER_URL" ||
    fail "Failed to download $SERVER_BINARY"

echo "Downloading checksums from: $CHECKSUM_URL"
"$CURL" -fsSL -o "$CHECKSUM_FILE" "$CHECKSUM_URL" ||
    fail "Failed to download checksums.txt"

EXPECTED_SERVER_SHA=$(awk -v name="$SERVER_BINARY" '$2 == name { print $1; exit }' "$CHECKSUM_FILE")
[ -n "$EXPECTED_SERVER_SHA" ] || fail "No checksum entry found for $SERVER_BINARY"
EXPECTED_CTL_SHA=$(awk -v name="$CTL_BINARY" '$2 == name { print $1; exit }' "$CHECKSUM_FILE")
EXPECTED_SOURCE_SHA=$(awk -v name="$SOURCE_ASSET" '$2 == name { print $1; exit }' "$CHECKSUM_FILE")

ACTUAL_SERVER_SHA=$(sha256_of "$SERVER_TMP_FILE") ||
    fail "sha256sum or shasum is required for checksum verification"

[ "$ACTUAL_SERVER_SHA" = "$EXPECTED_SERVER_SHA" ] ||
    fail "Checksum mismatch for $SERVER_BINARY"

echo "Checksum verified: $SERVER_BINARY $ACTUAL_SERVER_SHA"
if [ -n "$EXPECTED_CTL_SHA" ]; then
    echo "Downloading from: $CTL_URL"
    "$CURL" -fsSL -o "$CTL_TMP_FILE" "$CTL_URL" ||
        fail "Failed to download $CTL_BINARY"
    ACTUAL_CTL_SHA=$(sha256_of "$CTL_TMP_FILE") ||
        fail "sha256sum or shasum is required for checksum verification"
    [ "$ACTUAL_CTL_SHA" = "$EXPECTED_CTL_SHA" ] ||
        fail "Checksum mismatch for $CTL_BINARY"
    echo "Checksum verified: $CTL_BINARY $ACTUAL_CTL_SHA"
    INSTALL_CTL=1
else
    echo "No checksum entry found for $CTL_BINARY; skipping tntctl for this release"
fi

INSTALL_MANUALS=0
INSTALL_COMPLETIONS=0
MANUAL_LAYOUT=none
if [ -n "$EXPECTED_SOURCE_SHA" ]; then
    echo "Downloading from: $SOURCE_URL"
    "$CURL" -fsSL -o "$SOURCE_TMP_FILE" "$SOURCE_URL" ||
        fail "Failed to download $SOURCE_ASSET"
    ACTUAL_SOURCE_SHA=$(sha256_of "$SOURCE_TMP_FILE") ||
        fail "sha256sum or shasum is required for checksum verification"
    [ "$ACTUAL_SOURCE_SHA" = "$EXPECTED_SOURCE_SHA" ] ||
        fail "Checksum mismatch for $SOURCE_ASSET"
    echo "Checksum verified: $SOURCE_ASSET $ACTUAL_SOURCE_SHA"

    tar -xzf "$SOURCE_TMP_FILE" -C "$SOURCE_TMP_DIR" ||
        fail "Failed to extract $SOURCE_ASSET"
    SOURCE_ROOT="$SOURCE_TMP_DIR/TNT-${VERSION#v}"
    MANUAL_LAYOUT=standard
    for file in tntctl.1 tnt-message-log.5 tnt-chat.7 tnt-exec.7 \
            tnt-module-protocol.7 tnt.8; do
        [ -f "$SOURCE_ROOT/$file" ] || MANUAL_LAYOUT=legacy
    done
    if [ "$MANUAL_LAYOUT" = legacy ]; then
        [ -f "$SOURCE_ROOT/tnt.1" ] && [ -f "$SOURCE_ROOT/tntctl.1" ] ||
            fail "$SOURCE_ASSET has no installable manual pages"
    fi
    INSTALL_MANUALS=1

    INSTALL_COMPLETIONS=1
    for file in packaging/completions/tntctl.bash \
            packaging/completions/_tntctl \
            packaging/completions/tntctl.fish; do
        [ -f "$SOURCE_ROOT/$file" ] || INSTALL_COMPLETIONS=0
    done
else
    echo "No checked source asset; installing binaries only"
fi

# Install
chmod +x "$SERVER_TMP_FILE"
[ "$INSTALL_CTL" -eq 0 ] || chmod +x "$CTL_TMP_FILE"

INSTALL_PARENT=$INSTALL_PREFIX
while [ ! -e "$INSTALL_PARENT" ]; do
    next_parent=$(dirname "$INSTALL_PARENT")
    [ "$next_parent" != "$INSTALL_PARENT" ] || break
    INSTALL_PARENT=$next_parent
done

USE_SUDO=0
if [ ! -d "$INSTALL_PARENT" ] || [ ! -w "$INSTALL_PARENT" ]; then
    echo "Need sudo for installation to $INSTALL_PREFIX"
    need_cmd sudo
    USE_SUDO=1
fi

run_install() {
    if [ "$USE_SUDO" -eq 1 ]; then
        sudo install "$@"
    else
        install "$@"
    fi
}

run_install -d "$INSTALL_DIR"
run_install -m 755 "$SERVER_TMP_FILE" "$INSTALL_DIR/tnt"
[ "$INSTALL_CTL" -eq 0 ] ||
    run_install -m 755 "$CTL_TMP_FILE" "$INSTALL_DIR/tntctl"
if [ "$INSTALL_MANUALS" -eq 1 ] && [ "$MANUAL_LAYOUT" = standard ]; then
    run_install -d "$SHARE_DIR/man/man1"
    run_install -d "$SHARE_DIR/man/man5" "$SHARE_DIR/man/man7"
    run_install -d "$SHARE_DIR/man/man8"
    run_install -m 644 "$SOURCE_ROOT/tntctl.1" \
        "$SHARE_DIR/man/man1/tntctl.1"
    run_install -m 644 "$SOURCE_ROOT/tnt-message-log.5" \
        "$SHARE_DIR/man/man5/tnt-message-log.5"
    run_install -m 644 "$SOURCE_ROOT/tnt-chat.7" \
        "$SHARE_DIR/man/man7/tnt-chat.7"
    run_install -m 644 "$SOURCE_ROOT/tnt-exec.7" \
        "$SHARE_DIR/man/man7/tnt-exec.7"
    run_install -m 644 "$SOURCE_ROOT/tnt-module-protocol.7" \
        "$SHARE_DIR/man/man7/tnt-module-protocol.7"
    run_install -m 644 "$SOURCE_ROOT/tnt.8" "$SHARE_DIR/man/man8/tnt.8"
elif [ "$INSTALL_MANUALS" -eq 1 ]; then
    run_install -d "$SHARE_DIR/man/man1"
    run_install -m 644 "$SOURCE_ROOT/tnt.1" "$SHARE_DIR/man/man1/tnt.1"
    run_install -m 644 "$SOURCE_ROOT/tntctl.1" \
        "$SHARE_DIR/man/man1/tntctl.1"
fi

if [ "$INSTALL_COMPLETIONS" -eq 1 ]; then
    run_install -d "$SHARE_DIR/bash-completion/completions"
    run_install -d "$SHARE_DIR/zsh/site-functions"
    run_install -d "$SHARE_DIR/fish/vendor_completions.d"
    run_install -m 644 "$SOURCE_ROOT/packaging/completions/tntctl.bash" \
        "$SHARE_DIR/bash-completion/completions/tntctl"
    run_install -m 644 "$SOURCE_ROOT/packaging/completions/_tntctl" \
        "$SHARE_DIR/zsh/site-functions/_tntctl"
    run_install -m 644 "$SOURCE_ROOT/packaging/completions/tntctl.fish" \
        "$SHARE_DIR/fish/vendor_completions.d/tntctl.fish"
fi

echo ""
if [ "$INSTALL_CTL" -eq 1 ]; then
    echo "TNT installed successfully to $INSTALL_DIR/tnt and $INSTALL_DIR/tntctl"
else
    echo "TNT installed successfully to $INSTALL_DIR/tnt"
fi
[ "$INSTALL_MANUALS" -eq 0 ] ||
    echo "Manual pages installed under $SHARE_DIR/man"
echo ""
echo "Run with:"
echo "  tnt"
echo ""
echo "Or specify port:"
echo "  PORT=3333 tnt"
if [ "$INSTALL_CTL" -eq 1 ]; then
    echo ""
    echo "Control a server with:"
    echo "  tntctl localhost health"
fi
