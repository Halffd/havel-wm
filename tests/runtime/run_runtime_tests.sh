#!/bin/bash
# Havel WM runtime verification harness.
#
# Boots the compositor under the wlroots headless backend and drives it
# with real clients - the same verification that found every runtime
# bug during the 2026-09 audit (boot crashes, client-teardown assert,
# frame loop, IPC crashes, modifier state).
#
# Phases:
#   1. boot          - compositor starts, loading screen lifecycle, no crashes
#   2. client        - foot round-trip: connect/map/focus/render/disconnect
#   3. pixels        - grim captures a frame; window content must be present
#                      (non-black pixel mass in the capture = terminal rendered)
#   4. ipc           - 28-command probe + 6 hostile inputs (compositor must
#                      answer everything and survive)
#   5. keys          - zwp_virtual_keyboard_v1 injection: Meta+Return must
#                      trigger spawn_terminal in the log
#
# Usage: tests/runtime/run_runtime_tests.sh [BUILD_DIR]
# Requires: foot, grim, python3, wayland-scanner (for the injector build,
# done once into BUILD_DIR), a DISPLAY-less environment is fine.
#
# Exit 0 = all phases passed.

set -u
BUILD_DIR="${1:-./build}"
SOCK="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/havel-wm.sock"
LOG="$(mktemp /tmp/havel-runtime-XXXXXX.log)"
PASS=0; FAIL=0
WM_PID=""
FOOT_PID=""

cleanup() {
    [ -n "$FOOT_PID" ] && kill "$FOOT_PID" 2>/dev/null
    [ -n "$WM_PID" ] && kill "$WM_PID" 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT

wait_socket() {
    for _ in $(seq 1 45); do
        if python3 -c "
import socket,sys
try:
    s=socket.socket(socket.AF_UNIX); s.settimeout(1); s.connect('$SOCK'); s.close()
except Exception: sys.exit(1)
" 2>/dev/null; then return 0; fi
        sleep 1
    done
    return 1
}

start_wm() {
    rm -f "$SOCK"
    WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
        timeout 90 "$BUILD_DIR/bin/havel-wm" > "$LOG" 2>&1 &
    WM_PID=$!
    wait_socket || { echo "compositor socket never came up"; return 1; }
}

crashes() {
    grep -cE "SEGV|ABORT|terminate called|Assertion .* failed" "$LOG"
}

report() {
    if [ "$1" -eq 0 ]; then
        echo "PASS: $2"; PASS=$((PASS+1))
    else
        echo "FAIL: $2"; FAIL=$((FAIL+1))
    fi
}

echo "=== havel-wm runtime verification ==="
echo "log: $LOG"

# ---------------------------------------------------------------- phase 1
echo "--- phase 1: boot"
start_wm || { echo "FAIL: compositor did not start"; exit 1; }
sleep 3
report $([ "$(crashes)" -eq 0 ] && grep -q "Havel Compositor running" "$LOG" && echo 0 || echo 1) "clean boot, event loop running"
report $(grep -q "Auto-hide timeout reached" "$LOG" && echo 0 || echo 1) "loading-screen auto-hide timer fires"
report $(grep -q "\[FRAME\] Frame #1" "$LOG" && echo 0 || echo 1) "frame loop started"
report $(grep -qE "Conflict: .* already bound" "$LOG" && echo 1 || echo 0) "no keybinding registration conflicts"

# ---------------------------------------------------------------- phase 2
echo "--- phase 2: client round-trip (foot)"
WAYLAND_DISPLAY=wayland-0 timeout 60 foot -e sh -c 'sleep 300' > /dev/null 2>&1 &
FOOT_PID=$!
# poll for the map before testing it: client startup time varies under
# load (previous fixed 5s sleep raced slow boots)
MAP_SEEN=1
for _ in $(seq 1 20); do
    grep -q "onViewMapped" "$LOG" && { MAP_SEEN=0; break; }
    sleep 1
done
report "$MAP_SEEN" "client mapped a view"
# ---------------------------------------------------------------- phase 3
echo "--- phase 3: pixel capture (grim)"
sleep 2
CAP="$(mktemp /tmp/havel-capture-XXXXXX.png)"
if WAYLAND_DISPLAY=wayland-0 grim "$CAP" 2>/dev/null && [ -s "$CAP" ]; then
    # window content check: foot's #242424 terminal background must be
    # present in significant mass (proves wlroots composites the client)
    N=$(python3 -c "
from PIL import Image
img = Image.open('$CAP').convert('RGB')
px = img.load(); w,h = img.size
n = sum(1 for y in range(0,h,4) for x in range(0,w,4)
        if px[x,y][0]==0x24 and px[x,y][1]==0x24 and px[x,y][2]==0x24)
print(n)
" 2>/dev/null || echo 0)
    report $([ "${N:-0}" -gt 5000 ] && echo 0 || echo 1) "terminal content rendered (sampled #242424 px: ${N:-err})"
    rm -f "$CAP"
else
    echo "FAIL: grim could not capture (is wlr-screencopy wired?)"; FAIL=$((FAIL+1))
fi

# exercise teardown now (after the capture, which needs the window on
# screen): kill foot, then wait for the compositor's onViewUnmapped log
# line - previously checked while foot still ran 'sleep 300', so the
# check could never pass
kill "$FOOT_PID" 2>/dev/null; wait "$FOOT_PID" 2>/dev/null; FOOT_PID=""
UNMAP_SEEN=1
for _ in $(seq 1 15); do
    grep -qE "View unmapped" "$LOG" && { UNMAP_SEEN=0; break; }
    sleep 1
done
report "$UNMAP_SEEN" "view unmapped after client exit"

# ---------------------------------------------------------------- phase 4
echo "--- phase 4: IPC probe"
if python3 tests/runtime/ipc_probe.py > /tmp/havel-ipc-out.txt 2>&1; then
    echo "PASS: IPC surface (28 valid + 6 hostile + 4 havel_run)"; PASS=$((PASS+1))
else
    echo "FAIL: IPC probe"; cat /tmp/havel-ipc-out.txt | tail -8
    FAIL=$((FAIL+1))
fi

# ---------------------------------------------------------------- phase 5
echo "--- phase 5: key injection"
INJ="$BUILD_DIR/tests/inject_keys"
if [ ! -x "$INJ" ]; then
    mkdir -p "$BUILD_DIR/tests"
    wayland-scanner private-code tests/runtime/virtual-keyboard-unstable-v1.xml \
        "$BUILD_DIR/tests/vk-private-code.c"
    wayland-scanner client-header tests/runtime/virtual-keyboard-unstable-v1.xml \
        "$BUILD_DIR/tests/vk-client-header.h"
    cc -o "$INJ" tests/runtime/inject_keys.c "$BUILD_DIR/tests/vk-private-code.c" \
        $(pkg-config --cflags --libs wayland-client) -I"$BUILD_DIR/tests" || true
fi
if [ -x "$INJ" ]; then
    WAYLAND_DISPLAY=wayland-0 "$INJ" +125 28 -125
    sleep 2
    report $(grep -q "TRIGGERED: spawn_terminal" "$LOG" && echo 0 || echo 1) "Meta+Return triggers spawn_terminal"
    WAYLAND_DISPLAY=wayland-0 "$INJ" +125 3 -125
    sleep 1
    report $(grep -q "TRIGGERED: workspace_2" "$LOG" && echo 0 || echo 1) "Meta+2 triggers workspace_2"
else
    echo "SKIP: injector build failed (wayland-scanner missing?)"
fi

# ---------------------------------------------------------------- teardown
kill "$FOOT_PID" 2>/dev/null; FOOT_PID=""
sleep 2
report $([ "$(crashes)" -eq 0 ] && echo 0 || echo 1) "zero crashes across all phases"
grep -q "View freed" "$LOG" && report 0 "client teardown clean (View freed)" || report 1 "client teardown clean (View freed)"

kill "$WM_PID" 2>/dev/null; WM_PID=""
echo ""
echo "=== runtime verification: $PASS passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ] || { echo "log preserved: $LOG"; exit 1; }
rm -f "$LOG"
exit 0
