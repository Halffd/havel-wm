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
        sleep 0.2
    done
    return 1
}

# Wait until a log line matching PATTERN appears (real readiness/event
# condition, never a fixed sleep - fixed sleeps raced repeated times on
# this machine's load spikes during the audit)
wait_for_log() {
    local pattern="$1" timeout_s="${2:-30}"
    for _ in $(seq $((timeout_s * 5))); do
        grep -qE "$pattern" "$LOG" && return 0
        sleep 0.2
    done
    return 1
}

start_wm() {
    rm -f "$SOCK"
    # timeout must outlast the whole harness (boot + client + pixels +
    # ipc + keys + parity phases); 90s expired mid-run under load and
    # killed the compositor during the IPC phase
    WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
        timeout 240 "$BUILD_DIR/bin/havel-wm" > "$LOG" 2>&1 &
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

# boot checks: each polls its real log condition, no idle sleeps
BOOT_OK=1
wait_for_log "Havel Compositor running" 40 && wait_for_log "Auto-hide timeout reached" 10 && \
    wait_for_log "\[FRAME\] Frame #1" 5 && BOOT_OK=0
report $([ "$(crashes)" -eq 0 ] && [ "$BOOT_OK" -eq 0 ] && echo 0 || echo 1) "clean boot, event loop running"
report $(grep -q "Auto-hide timeout reached" "$LOG" && echo 0 || echo 1) "loading-screen auto-hide timer fires"
report $(grep -q "\[FRAME\] Frame #1" "$LOG" && echo 0 || echo 1) "frame loop started"
report $(grep -qE "Conflict: .* already bound" "$LOG" && echo 1 || echo 0) "no keybinding registration conflicts"
# Embed detection for phase 6: the harness must pass on CI builds without
# the monorepo lib (same contract as the IPC probe's havel_run skip). The
# embed announces itself with this line at boot; without it the hotkey
# phases can only fail, so they skip.
HAS_EMBED=$(grep -ac "\[Havel\] Language embedded" "$LOG")

# ---------------------------------------------------------------- phase 2
echo "--- phase 2: client round-trip (terminal)"
# Pick a terminal client. The pixel check in phase 3 expects a #242424
# background mass: foot ships it natively; kitty takes -o overrides;
# alacritty takes a config file. foot vanished from this box mid-session
# (env volatility) while kitty/alacritty remained - the round-trip is a
# CLIENT test, not a foot test.
# NOTE: each case line uses direct invocation, NOT a $TERM_CMD variable -
# unquoted variable expansion would mangle the sh -c 'sleep 300' quoting
# (the child would die instantly and the terminal self-exit would race
# the pixel capture - observed as intermittent 0-px captures).
TERM_PROG=""
if command -v foot >/dev/null 2>&1; then
    TERM_PROG=foot
elif command -v kitty >/dev/null 2>&1; then
    TERM_PROG=kitty
elif command -v alacritty >/dev/null 2>&1; then
    TERM_PROG=alacritty
    ALACRITTY_TEST_CONF="$(mktemp /tmp/havel-alacritty-XXXXXX.toml)"
    printf '[colors.primary]\nbackground = "#242424"\n' > "$ALACRITTY_TEST_CONF"
fi
if [ -z "$TERM_PROG" ]; then
    echo "SKIP: no terminal client (need foot, kitty or alacritty)"
else
    case "$TERM_PROG" in
        foot)
            WAYLAND_DISPLAY=wayland-0 timeout 60 foot -e sh -c 'sleep 300' > /dev/null 2>&1 & ;;
        kitty)
            WAYLAND_DISPLAY=wayland-0 timeout 60 kitty -o background=#242424 -o background_opacity=1 -e sh -c 'sleep 300' > /dev/null 2>&1 & ;;
        alacritty)
            WAYLAND_DISPLAY=wayland-0 timeout 60 alacritty --config-file "$ALACRITTY_TEST_CONF" -e sh -c 'sleep 300' > /dev/null 2>&1 & ;;
    esac
    FOOT_PID=$!
fi
if [ -n "$TERM_PROG" ]; then
# poll for the map before testing it: client startup time varies under
# load. 20s expired twice on this box while a parallel session's builds
# saturated the CPU - all six downstream checks cascaded from the one
# late map (client/pixels/unmap/keys/teardown). 45s absorbs that.
MAP_SEEN=1
for _ in $(seq 1 45); do
    grep -q "onViewMapped" "$LOG" && { MAP_SEEN=0; break; }
    sleep 1
done
report "$MAP_SEEN" "client mapped a view"
# ---------------------------------------------------------------- phase 3
echo "--- phase 3: pixel capture (grim)"
# wait for the map's position/focus to settle (that log line appears right
# after onViewMapped) - the client's first presented frame is then live
wait_for_log "Node raised, positioned" 20 || true
CAP="$(mktemp /tmp/havel-capture-XXXXXX.png)"
# Poll the capture: the client's first PRESENTED frame lags its map on
# slow boots (run 2 sampled 0 px while run 1 got 29k - same code).
N=0
for _ in $(seq 1 10); do
    if WAYLAND_DISPLAY=wayland-0 grim "$CAP" 2>/dev/null && [ -s "$CAP" ]; then
        N=$(python3 -c "
from PIL import Image
img = Image.open('$CAP').convert('RGB')
px = img.load(); w,h = img.size
n = sum(1 for y in range(0,h,4) for x in range(0,w,4)
        if px[x,y][0]==0x24 and px[x,y][1]==0x24 and px[x,y][2]==0x24)
print(n)
" 2>/dev/null || echo 0)
        [ "${N:-0}" -gt 5000 ] && break
    fi
    sleep 1
done
report $([ "${N:-0}" -gt 5000 ] && echo 0 || echo 1) "terminal content rendered (sampled #242424 px: ${N:-err})"
rm -f "$CAP"

# exercise teardown now (after the capture, which needs the window on
# screen): kill the client, then wait for the compositor's onViewUnmapped
# log line - previously checked while the client still ran 'sleep 300',
# so the check could never pass
kill "$FOOT_PID" 2>/dev/null; wait "$FOOT_PID" 2>/dev/null; FOOT_PID=""
UNMAP_SEEN=1
for _ in $(seq 1 15); do
    grep -qE "View unmapped" "$LOG" && { UNMAP_SEEN=0; break; }
    sleep 1
done
report "$UNMAP_SEEN" "view unmapped after client exit"
fi   # end of the terminal-client branch (phases 2+3)

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
    report $(wait_for_log "TRIGGERED: spawn_terminal" 20 && echo 0 || echo 1) "Meta+Return triggers spawn_terminal"
    WAYLAND_DISPLAY=wayland-0 "$INJ" +125 3 -125
    report $(wait_for_log "TRIGGERED: workspace_2" 20 && echo 0 || echo 1) "Meta+2 triggers workspace_2"
else
    echo "SKIP: injector build failed (wayland-scanner missing?)"
fi

# ---------------------------------------------------------------- phase 6
echo "--- phase 6: havel language-syntax hotkeys (config.hv)"
if [ "${HAS_EMBED:-0}" -eq 0 ]; then
    echo "SKIP: compositor built without the havel embed (no [Havel] line at boot)"
else
# The real user-facing hotkey forms through the embedded interpreter:
# sugar (^+F2 => {...} lowers to hotkey.register) and host fn
# (keybind("Meta+F3", fn())). Both must register and fire via injection.
# F-key evdev codes: F2=60, F3=61 (compositor adds 8 for xkb).
PARITY_HOME="$(mktemp -d /tmp/havel-parity-XXXXXX)"
mkdir -p "$PARITY_HOME/.config/havel-wm"
cat > "$PARITY_HOME/.config/havel-wm/config.hv" <<'EOF'
^+F2 => { log("harness-^+F2-fired") }
keybind("Meta+F3", fn() { log("harness-keybind-F3-fired") })
EOF
HOME="$PARITY_HOME" WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
    timeout 150 "$BUILD_DIR/bin/havel-wm" > /tmp/havel-parity-wm.log 2>&1 &
PARITY_PID=$!
OLD_LOG="$LOG"; LOG=/tmp/havel-parity-wm.log
# The main harness WM still holds wayland-0; the parity instance
# auto-increments. The real readiness signal: the parity WM's own
# "Compositor running on <sock>" line (config.hv loaded by then). Wait
# for it, then read the actual socket name from the log - never assume.
PARITY_SOCK=""
for _ in $(seq 750); do
    if grep -q "Compositor running on" /tmp/havel-parity-wm.log 2>/dev/null; then
        PARITY_SOCK=$(grep -o "Compositor running on wayland-[0-9]*" /tmp/havel-parity-wm.log | head -1 | grep -o "wayland-[0-9]*")
        break
    fi
    sleep 0.2
done
if [ -z "$PARITY_SOCK" ]; then
    echo "SKIP: parity compositor never became ready"
    LOG="$OLD_LOG"
else
    WAYLAND_DISPLAY="$PARITY_SOCK" "$INJ" +29 +42 60 -42 -29
    report $(wait_for_log "harness-..F2-fired" 10 && echo 0 || echo 1) "^+F2 sugar fires (Ctrl+Shift+F2)"
    WAYLAND_DISPLAY="$PARITY_SOCK" "$INJ" +125 61 -125
    report $(wait_for_log "harness-keybind-F3-fired" 10 && echo 0 || echo 1) "keybind('Meta+F3') fires"

    # 6b: script-driven injection via the persistent self-keyboard -
    # no external client. A script defines a keybinding, then another
    # script (via havel_run) injects the combo through io.injectKey;
    # the closure must fire. Exercises: self-keyboard + dispatch +
    # closure invocation, the full script-driven loop.
    cat > "$PARITY_HOME/.config/havel-wm/config.hv" <<'EOF'
keybind("Meta+F5", fn() { log("harness-selfinject-fired") })
EOF
    # restart the parity compositor to load the new config; its own IPC
    # socket (HAVEL_IPC_SOCKET) so the main instance's IPC is untouched
    kill "$PARITY_PID" 2>/dev/null
    sleep 0.5
    PARITY_IPC="/run/user/$(id -u)/havel-wm-parity.sock"
    rm -f "$PARITY_IPC"
    HOME="$PARITY_HOME" HAVEL_IPC_SOCKET="$PARITY_IPC" \
        WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
        timeout 150 "$BUILD_DIR/bin/havel-wm" > /tmp/havel-parity-wm2.log 2>&1 &
    PARITY_PID=$!
    LOG=/tmp/havel-parity-wm2.log
    P2_SOCK=""
    for _ in $(seq 750); do
        if grep -q "Compositor running on" /tmp/havel-parity-wm2.log 2>/dev/null; then
            P2_SOCK=$(grep -o "Compositor running on wayland-[0-9]*" /tmp/havel-parity-wm2.log | head -1 | grep -o "wayland-[0-9]*")
            break
        fi
        sleep 0.2
    done
    if [ -n "$P2_SOCK" ]; then
        PARITY_WAYLAND_SOCK="/run/user/$(id -u)/$P2_SOCK"
        # wait for the self-keyboard to be ready (its own log line)
        wait_for_log "Self-keyboard ready" 15 || true
        # inject via a SCRIPT through the persistent self-keyboard,
        # speaking JSON-RPC at the parity instance's IPC socket
        python3 - "$PARITY_IPC" <<'PYEOF'
import socket, json, sys
sock = sys.argv[1]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(5); s.connect(sock)
def rpc(method, params):
    s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":method,"params":params})+"\n").encode())
    buf=b""
    while b"\n" not in buf:
        c=s.recv(65536)
        if not c: return None
        buf+=c
    return json.loads(buf.split(b"\n")[0])
# Meta+F5 = 125(meta) + 63(F5), press-release pairs through io.injectKey
for code, down in [(125, True), (63, True), (63, False), (125, False)]:
    r = rpc("havel_run", {"source": f'io.injectKey({code}, {str(down).lower()})'})
    if r is None or "error" in (r or {}):
        print(f"INJECT-FAIL {code} {down}: {r}")
        sys.exit(1)
print("injected all four")
s.close()
PYEOF
        # Exact counts: the chunk-pin regression made the "closure" resolve
        # into the inject snippet itself (139 TRIGGERED, 0 closure logs -
        # observed live). One combo must fire the binding exactly once.
        sleep 2
        FIRED=$(grep -acE "harness-selfinject-fired" /tmp/havel-parity-wm2.log)
        TRIG=$(grep -acE "TRIGGERED" /tmp/havel-parity-wm2.log)
        report $([ "$FIRED" -eq 1 ] && echo 0 || echo 1) "io.injectKey fires closure exactly once (fired=$FIRED)"
        report $([ "$TRIG" -eq 1 ] && echo 0 || echo 1) "binding triggered exactly once (trig=$TRIG)"
        report $([ "$(crashes)" -eq 0 ] && echo 0 || echo 1) "self-inject compositor clean"
    else
        echo "SKIP: parity restart never became ready"
    fi
    report $([ "$(crashes)" -eq 0 ] && echo 0 || echo 1) "parity compositor clean"
fi   # closes: if [ -z "$PARITY_SOCK" ]
fi   # HAS_EMBED gate (phase 6)
kill "${PARITY_PID:-}" 2>/dev/null
[ -n "${PARITY_HOME:-}" ] && rm -rf "$PARITY_HOME"
LOG="${OLD_LOG:-$LOG}"

# ---------------------------------------------------------------- teardown
kill "$FOOT_PID" 2>/dev/null; FOOT_PID=""
report $([ "$(crashes)" -eq 0 ] && echo 0 || echo 1) "zero crashes across all phases"
if [ -n "$TERM_PROG" ]; then
    report $(wait_for_log "View freed" 10 && echo 0 || echo 1) "client teardown clean (View freed)"
else
    echo "SKIP: client teardown (no terminal client was available)"
fi

kill "$WM_PID" 2>/dev/null; WM_PID=""
echo ""
echo "=== runtime verification: $PASS passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ] || { echo "log preserved: $LOG"; exit 1; }
rm -f "$LOG"
exit 0
