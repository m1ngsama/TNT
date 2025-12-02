#!/bin/sh
# TNT Auto-deploy script
# Usage: curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh

set -e

VERSION=${VERSION:-latest}
INSTALL_DIR=${INSTALL_DIR:-/usr/local/bin}

# Detect OS and architecture
OS=$(uname -s | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)

case "$ARCH" in
    x86_64) ARCH="amd64" ;;
    aarch64|arm64) ARCH="arm64" ;;
    *) echo "Unsupported architecture: $ARCH"; exit 1 ;;
esac

BINARY="tnt-${OS}-${ARCH}"
REPO="m1ngsama/TNT"

echo "=== TNT Installer ==="
echo "OS: $OS"
echo "Arch: $ARCH"
echo "Version: $VERSION"
echo ""

# Get latest version if not specified
if [ "$VERSION" = "latest" ]; then
    echo "Fetching latest version..."
    VERSION=$(curl -sSL "https://api.github.com/repos/$REPO/releases/latest" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/')
fi

echo "Installing version: $VERSION"

# Download
URL="https://github.com/$REPO/releases/download/$VERSION/$BINARY"
echo "Downloading from: $URL"

TMP_FILE=$(mktemp)
if ! curl -sSL -o "$TMP_FILE" "$URL"; then
    echo "ERROR: Failed to download $BINARY"
    rm -f "$TMP_FILE"
    exit 1
fi

# Install
chmod +x "$TMP_FILE"

if [ -w "$INSTALL_DIR" ]; then
    mv "$TMP_FILE" "$INSTALL_DIR/tnt"
else
    echo "Need sudo for installation to $INSTALL_DIR"
    sudo mv "$TMP_FILE" "$INSTALL_DIR/tnt"
fi

echo ""
echo "✓ TNT installed successfully to $INSTALL_DIR/tnt"
echo ""
echo "Run with:"
echo "  tnt"
echo ""
echo "Or specify port:"
echo "  PORT=3333 tnt"
