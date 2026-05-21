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

BINARY="tnt-${OS}-${ARCH}"

echo "=== TNT Installer ==="
echo "OS: $OS"
echo "Arch: $ARCH"
echo "Version: $VERSION"
echo ""

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
URL="https://github.com/$REPO/releases/download/$VERSION/$BINARY"
CHECKSUM_URL="https://github.com/$REPO/releases/download/$VERSION/checksums.txt"
echo "Downloading from: $URL"

TMP_FILE=$(mktemp "${TMPDIR:-/tmp}/tnt.XXXXXX")
CHECKSUM_FILE=$(mktemp "${TMPDIR:-/tmp}/tnt-checksums.XXXXXX")
cleanup() {
    rm -f "$TMP_FILE" "$CHECKSUM_FILE"
}
trap cleanup EXIT INT TERM

curl -fsSL -o "$TMP_FILE" "$URL" || fail "Failed to download $BINARY"

echo "Downloading checksums from: $CHECKSUM_URL"
curl -fsSL -o "$CHECKSUM_FILE" "$CHECKSUM_URL" ||
    fail "Failed to download checksums.txt"

EXPECTED_SHA=$(awk -v name="$BINARY" '$2 == name { print $1; exit }' "$CHECKSUM_FILE")
[ -n "$EXPECTED_SHA" ] || fail "No checksum entry found for $BINARY"

ACTUAL_SHA=$(sha256_of "$TMP_FILE") ||
    fail "sha256sum or shasum is required for checksum verification"

[ "$ACTUAL_SHA" = "$EXPECTED_SHA" ] ||
    fail "Checksum mismatch for $BINARY"

echo "Checksum verified: $ACTUAL_SHA"

# Install
chmod +x "$TMP_FILE"

if [ -d "$INSTALL_DIR" ] && [ -w "$INSTALL_DIR" ]; then
    install -m 755 "$TMP_FILE" "$INSTALL_DIR/tnt"
else
    echo "Need sudo for installation to $INSTALL_DIR"
    need_cmd sudo
    sudo mkdir -p "$INSTALL_DIR"
    sudo install -m 755 "$TMP_FILE" "$INSTALL_DIR/tnt"
fi

echo ""
echo "TNT installed successfully to $INSTALL_DIR/tnt"
echo ""
echo "Run with:"
echo "  tnt"
echo ""
echo "Or specify port:"
echo "  PORT=3333 tnt"
