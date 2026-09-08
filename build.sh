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
# Cap parallel jobs: default nproc parallelism OOM-kills cc1plus on this box
# (multiple large Qt/wayland TUs compiled simultaneously). 2 is the verified
# stable value; override with HAVEL_BUILD_JOBS if you have more RAM.
JOBS="${HAVEL_BUILD_JOBS:-2}"
echo "Building... (-j${JOBS})"
cmake --build . -j"${JOBS}"

echo ""
echo "=== Build Complete ==="
echo "Binaries are in: ${BUILD_DIR}/bin/"
echo ""
echo "To install, run:"
echo "  sudo cmake --install ${BUILD_DIR}"
echo ""
echo "Or use the install script:"
echo "  ./install.sh"
