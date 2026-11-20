#!/bin/bash
# Verify that --renderer opengl overrides an inherited pixman selection
# before wlroots creates the window renderer.
#
# Expected result: wlroots loads WLR_RENDERER=gles2 and creates its GLES2
# renderer. The compositor is stopped as soon as that real startup event
# is observed.

set -u

BIN="${1:-./build/bin/havel-wm}"
LOG="$(mktemp /tmp/opencode/havel-renderer-smoke-XXXXXX.log)"
RUNTIME_DIR="$(mktemp -d /tmp/opencode/havel-renderer-runtime-XXXXXX)"
WM_PID=""

cleanup() {
    if [ -n "$WM_PID" ] && kill -0 "$WM_PID" 2>/dev/null; then
        kill "$WM_PID" 2>/dev/null
    fi
    if [ -n "$WM_PID" ]; then
        wait "$WM_PID" 2>/dev/null
    fi
    rm -f "$LOG"
    rm -rf "$RUNTIME_DIR"
}
trap cleanup EXIT

if [ ! -x "$BIN" ]; then
    echo "FAIL renderer smoke: executable not found: $BIN"
    exit 1
fi

XDG_RUNTIME_DIR="$RUNTIME_DIR" \
WLR_BACKENDS=headless \
WLR_HEADLESS_OUTPUTS=1 \
WLR_RENDERER=pixman \
LIBGL_ALWAYS_SOFTWARE=1 \
    "$BIN" --renderer opengl --no-loading-screen >"$LOG" 2>&1 &
WM_PID=$!

READY=0
for _ in $(seq 1 300); do
    if grep -Fq "Creating GLES2 renderer" "$LOG"; then
        READY=1
        break
    fi
    if ! kill -0 "$WM_PID" 2>/dev/null; then
        break
    fi
    sleep 0.2
done

if [ "$READY" -ne 1 ]; then
    echo "FAIL renderer smoke: GLES2 renderer was not created"
    tail -40 "$LOG"
    exit 1
fi

if ! grep -Fq "[MAIN] WLR_RENDERER=gles2 selected before wlroots renderer creation" "$LOG"; then
    echo "FAIL renderer smoke: CLI selection was not applied before renderer creation"
    tail -40 "$LOG"
    exit 1
fi

if ! grep -Fq "Loading WLR_RENDERER option: gles2" "$LOG"; then
    echo "FAIL renderer smoke: wlroots did not load the gles2 renderer selection"
    tail -40 "$LOG"
    exit 1
fi

if grep -Fq "Loading WLR_RENDERER option: pixman" "$LOG"; then
    echo "FAIL renderer smoke: inherited pixman selection was not overridden"
    tail -40 "$LOG"
    exit 1
fi

echo "OK renderer smoke: --renderer opengl created the wlroots GLES2 window renderer"
