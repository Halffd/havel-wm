#!/bin/bash
# Install script for Havel WM

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
PREFIX="${PREFIX:-/usr/local}"

echo "=== Havel WM Install ==="
echo "Install prefix: ${PREFIX}"

# Check if build exists
if [ ! -d "${BUILD_DIR}" ]; then
    echo "Error: Build directory not found."
    echo "Please run ./build.sh first."
    exit 1
fi

# Install
echo "Installing..."
sudo cmake --install "${BUILD_DIR}" --prefix "${PREFIX}"

echo ""
echo "=== Installation Complete ==="
echo ""
echo "Havel WM has been installed to ${PREFIX}"
echo ""
echo "To use Havel WM:"
echo "1. Log out of your current session"
echo "2. At the login screen, select 'Havel WM' from the session menu"
echo "3. Log in"
echo ""
echo "Or start manually from TTY:"
echo "  havel-wm"
echo ""
echo "To start the panel (in another terminal):"
echo "  havel-panel"
