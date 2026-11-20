#!/bin/bash
# Configure, build, and run the compositor test harness on Void Linux.
# Run inside ci/void-linux.Dockerfile's dependency image with the source
# tree mounted at /src.

set -euo pipefail

BUILD_DIR="${BUILD_DIR:-/tmp/havel-void-build}"
BUILD_JOBS="${BUILD_JOBS:-2}"

cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_HAVEL_LANG=OFF \
    -DENABLE_ASAN=OFF
cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/havel-void-runtime}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

bash tests/runtime/run_runtime_tests.sh "$BUILD_DIR"
