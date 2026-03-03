#!/bin/bash
# Build script for Havel WM

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

echo "=== Havel WM Build ==="
echo "Build directory: ${BUILD_DIR}"

# Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Configure
echo "Configuring..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    "$@"

# Build
echo "Building..."
cmake --build . -j"$(nproc)"

echo ""
echo "=== Build Complete ==="
echo "Binaries are in: ${BUILD_DIR}/bin/"
echo ""
echo "To install, run:"
echo "  sudo cmake --install ${BUILD_DIR}"
echo ""
echo "Or use the install script:"
echo "  ./install.sh"
