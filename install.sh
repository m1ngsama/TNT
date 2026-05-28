#!/bin/sh
# TNT installer
# Usage: curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh

set -e

VERSION=${VERSION:-latest}
INSTALL_DIR=${INSTALL_DIR:-/usr/local/bin}
REPO="m1ngsama/TNT"

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

need_cmd curl
need_cmd awk

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

# Get latest version if not specified
if [ "$VERSION" = "latest" ]; then
    echo "Fetching latest version..."
    VERSION=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" |
        sed -n 's/.*"tag_name":[[:space:]]*"\([^"]*\)".*/\1/p' |
        head -n 1)
    [ -n "$VERSION" ] || fail "Could not determine latest release version"
fi

echo "Installing version: $VERSION"

# Download
SERVER_URL="https://github.com/$REPO/releases/download/$VERSION/$SERVER_BINARY"
CTL_URL="https://github.com/$REPO/releases/download/$VERSION/$CTL_BINARY"
CHECKSUM_URL="https://github.com/$REPO/releases/download/$VERSION/checksums.txt"
echo "Downloading from: $SERVER_URL"

SERVER_TMP_FILE=$(mktemp "${TMPDIR:-/tmp}/tnt.XXXXXX")
CTL_TMP_FILE=$(mktemp "${TMPDIR:-/tmp}/tntctl.XXXXXX")
CHECKSUM_FILE=$(mktemp "${TMPDIR:-/tmp}/tnt-checksums.XXXXXX")
INSTALL_CTL=0
cleanup() {
    rm -f "$SERVER_TMP_FILE" "$CTL_TMP_FILE" "$CHECKSUM_FILE"
}
trap cleanup EXIT INT TERM

curl -fsSL -o "$SERVER_TMP_FILE" "$SERVER_URL" ||
    fail "Failed to download $SERVER_BINARY"

echo "Downloading checksums from: $CHECKSUM_URL"
curl -fsSL -o "$CHECKSUM_FILE" "$CHECKSUM_URL" ||
    fail "Failed to download checksums.txt"

EXPECTED_SERVER_SHA=$(awk -v name="$SERVER_BINARY" '$2 == name { print $1; exit }' "$CHECKSUM_FILE")
[ -n "$EXPECTED_SERVER_SHA" ] || fail "No checksum entry found for $SERVER_BINARY"
EXPECTED_CTL_SHA=$(awk -v name="$CTL_BINARY" '$2 == name { print $1; exit }' "$CHECKSUM_FILE")

ACTUAL_SERVER_SHA=$(sha256_of "$SERVER_TMP_FILE") ||
    fail "sha256sum or shasum is required for checksum verification"

[ "$ACTUAL_SERVER_SHA" = "$EXPECTED_SERVER_SHA" ] ||
    fail "Checksum mismatch for $SERVER_BINARY"

echo "Checksum verified: $SERVER_BINARY $ACTUAL_SERVER_SHA"
if [ -n "$EXPECTED_CTL_SHA" ]; then
    echo "Downloading from: $CTL_URL"
    curl -fsSL -o "$CTL_TMP_FILE" "$CTL_URL" ||
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

# Install
chmod +x "$SERVER_TMP_FILE"
[ "$INSTALL_CTL" -eq 0 ] || chmod +x "$CTL_TMP_FILE"

if [ -d "$INSTALL_DIR" ] && [ -w "$INSTALL_DIR" ]; then
    install -m 755 "$SERVER_TMP_FILE" "$INSTALL_DIR/tnt"
    [ "$INSTALL_CTL" -eq 0 ] || install -m 755 "$CTL_TMP_FILE" "$INSTALL_DIR/tntctl"
else
    echo "Need sudo for installation to $INSTALL_DIR"
    need_cmd sudo
    sudo mkdir -p "$INSTALL_DIR"
    sudo install -m 755 "$SERVER_TMP_FILE" "$INSTALL_DIR/tnt"
    [ "$INSTALL_CTL" -eq 0 ] || sudo install -m 755 "$CTL_TMP_FILE" "$INSTALL_DIR/tntctl"
fi

echo ""
if [ "$INSTALL_CTL" -eq 1 ]; then
    echo "TNT installed successfully to $INSTALL_DIR/tnt and $INSTALL_DIR/tntctl"
else
    echo "TNT installed successfully to $INSTALL_DIR/tnt"
fi
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
