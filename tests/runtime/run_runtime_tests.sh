#!/bin/bash
# Havel WM runtime verification harness.
#
# Boots the compositor under the wlroots headless backend and drives it
# with real clients - the same verification that found every runtime
# bug during the 2026-09 audit (boot crashes, client-teardown assert,
# frame loop, IPC crashes, modifier state).
#
# Phases:
#   0. unit          - compositor-free unit suites: font module (bitmap
#                     and vector faces through the one FreeType path),
#                     Common.h utilities, keybinding parser/manager,
#                     renderer selection
#   0b. renderer     - --renderer opengl overrides inherited pixman before
#                     wlroots creates the real GLES2 window renderer
#   1. boot          - compositor starts, loading screen lifecycle, no crashes
#   2. client        - foot round-trip: connect/map/focus/render/disconnect
#   3. pixels        - grim captures a frame; window content must be present
#                      (non-black pixel mass in the capture = terminal rendered)
#   4. ipc           - 28-command probe + 6 hostile inputs (compositor must
#                      answer everything and survive)
#   5. keys          - zwp_virtual_keyboard_v1 injection: Meta+Return must
#                      trigger spawn_terminal in the log
#   8. wallpaper      - set_wallpaper via IPC draws a generated pattern and
#                      clears back to the solid background
#
# Usage: tests/runtime/run_runtime_tests.sh [BUILD_DIR]
#
# The summary counts skipped checks separately and lists the reasons: a
# skipped check is not a pass. Set HAVEL_HARNESS_FORCE_SKIP_TERMINAL=1 to
# exercise the no-terminal path (and the counters) on a box that has
# terminals.
# Requires: foot, grim, python3, wayland-scanner (for the injector build,
# done once into BUILD_DIR), a DISPLAY-less environment is fine.
#
# Exit 0 = all phases passed.

set -u
BUILD_DIR="${1:-./build}"
SOCK="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/havel-wm.sock"
LOG="$(mktemp /tmp/havel-runtime-XXXXXX.log)"
PASS=0; FAIL=0; SKIPPED=0
WM_PID=""
FOOT_PID=""

cleanup() {
    [ -n "$FOOT_PID" ] && kill "$FOOT_PID" 2>/dev/null
    [ -n "$WM_PID" ] && kill "$WM_PID" 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT

wait_socket() {
    # 9s was too tight even before: under this box's I/O load spikes a
    # fresh compositor boot (117MB binary, split-DWARF) has taken 45s at
    # load ~10 and ~25s at load ~5. Load 20+ (external build storms)
    # stretched boots past 90s; a later run at load 27-36 failed the boot
    # checks outright, so: 1500 x 0.2s = 300s. The check stays real
    # (connect against a live listener) - a dead compositor still fails.
    for _ in $(seq 1 1500); do
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

# Note on the IPC timeouts below (settimeout(30) in every inline python
# probe, and in ipc_probe.py): 10s was enough on an idle box but not on
# this one. A run that stretched to 70 minutes under load saw the
# compositor render 600 frames in 44-88s instead of the normal 10
# (measured from its own FRAME lines), and every IPC call that hit the
# old 10s limit failed wholesale (IPC probe, settings_set, repl,
# workspace move, opacity) while the compositor stayed alive and all
# other checks passed. The loop is never dead in those runs, only slow -
# so the client waits; a genuinely dead compositor still fails every
# check.

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
    # Clean every wayland socket, not just wayland-0: a leftover socket
    # from a crashed manual instance shifts the auto-increment, so the
    # compositor ends up on wayland-1 while every phase (and this check)
    # waits on wayland-0 - "compositor socket never came up" with a
    # healthy process (observed).
    rm -f "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"/wayland-* 2>/dev/null
    # timeout must outlast the whole harness (boot + client + pixels +
    # ipc + keys + parity + alttab + wallpaper phases); 90s expired
    # mid-run once, then 240s, then 420s (during an external build the
    # run stretched past it: the main WM was SIGTERMed mid-phase, and
    # every later phase talked to a dead compositor). 5400s is a safety
    # net only - the trap kills the WM at harness exit either way.
    WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
        timeout 5400 "$BUILD_DIR/bin/havel-wm" > "$LOG" 2>&1 &
    WM_PID=$!
    wait_socket || { echo "compositor socket never came up"; return 1; }
}

crashes() {
    grep -cE "SEGV|ABORT|terminate called|Assertion .* failed" "$LOG"
}

# A skipped check is not a pass. Counting them keeps the summary honest:
# an environment gap used to look like a smaller-but-green run (observed:
# 21 passed, 0 failed while three checks never ran).
skip() {
    echo "SKIP: $1"
    SKIPPED=$((SKIPPED+1))
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

# ---------------------------------------------------------------- phase 0
echo "--- phase 0: unit tests (no compositor needed)"
# Pure unit binaries built alongside the compositor. The font module's
# test generates its own bitmap font at run time, so it needs nothing
# from the environment.
for U in "$BUILD_DIR/test-font" "$BUILD_DIR/test-utilities" "$BUILD_DIR/test-keybinding" "$BUILD_DIR/test-keybinding-manager" "$BUILD_DIR/test-renderer-selection" "$BUILD_DIR/test-app-icons"; do
    NAME=$(basename "$U")
    if [ -x "$U" ]; then
        # 60s was too short on a loaded box: a run that stretched to 100
        # minutes reported test-app-icons with NO output (killed at the
        # limit), while the same binary runs in seconds on an idle box.
        if timeout 300 "$U" > /tmp/havel-unit-"$NAME".log 2>&1; then
            report 0 "$NAME unit suite ($(tail -1 /tmp/havel-unit-"$NAME".log))"
        else
            report 1 "$NAME unit suite ($(tail -1 /tmp/havel-unit-"$NAME".log))"
            cat /tmp/havel-unit-"$NAME".log | tail -8
        fi
    else
        skip "$NAME (not built - cmake --build build --target $NAME)"
    fi
done

# ---------------------------------------------------------------- phase 0b
echo "--- phase 0b: renderer startup"
if tests/runtime/renderer_smoke.sh "$BUILD_DIR/bin/havel-wm" > /tmp/havel-renderer-smoke-out.txt 2>&1; then
    report 0 "OpenGL renderer startup ($(tail -1 /tmp/havel-renderer-smoke-out.txt))"
else
    report 1 "OpenGL renderer startup ($(tail -1 /tmp/havel-renderer-smoke-out.txt))"
    cat /tmp/havel-renderer-smoke-out.txt | tail -12
fi

# ---------------------------------------------------------------- phase 1
echo "--- phase 1: boot"
start_wm || { echo "FAIL: compositor did not start"; exit 1; }

# boot checks: each polls its real log condition, no idle sleeps. The
# waits are sized for a starved loop: the loading-screen timer runs on
# the frame loop, so at load ~44 the "Auto-hide timeout reached" line had
# still not appeared after 10s while the frame loop itself was demonstrably
# running ("frame loop started" passed in the same phase).
BOOT_OK=1
wait_for_log "Havel Compositor running" 120 && wait_for_log "Auto-hide timeout reached" 60 && \
    wait_for_log "\[FRAME\] Frame #1" 30 && BOOT_OK=0
report $([ "$(crashes)" -eq 0 ] && [ "$BOOT_OK" -eq 0 ] && echo 0 || echo 1) "clean boot, event loop running"
report $(grep -q "Auto-hide timeout reached" "$LOG" && echo 0 || echo 1) "loading-screen auto-hide timer fires"

# The splash hiding is a full-screen repaint: the cursor-damage class of
# bug (repaint regions never reaching the output) left its background on
# screen as a stale dark region with hard edges - a user screenshot
# showed exactly that. Assert the framebuffer no longer contains the
# splash background colour once the hide has fired.
LSCAP="$(mktemp /tmp/havel-ls-XXXXXX.png)"
if WAYLAND_DISPLAY=wayland-0 grim "$LSCAP" 2>/dev/null && [ -s "$LSCAP" ]; then
    LSPX=$(python3 -c "
from PIL import Image
from collections import Counter
im = Image.open('$LSCAP').convert('RGB'); px = im.load(); w, h = im.size
c = Counter(px[x, y] for x in range(0, w, 8) for y in range(0, h, 8))
print(sum(n for k, n in c.items()
         if abs(k[0]-13) < 4 and abs(k[1]-13) < 4 and abs(k[2]-26) < 5))
" 2>/dev/null || echo -1)
    report $([ "${LSPX:-1}" -eq 0 ] && echo 0 || echo 1) "splash cleared from the framebuffer (stale px: ${LSPX:-err})"
    rm -f "$LSCAP"
else
    rm -f "$LSCAP"
    skip "splash framebuffer check (no capture)"
fi
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
# Test hook: force the "no terminal client" path so the skip counting can
# be exercised on a box that HAS terminals (otherwise the skip path only
# runs when the environment loses them, which is not reproducible).
if [ -n "${HAVEL_HARNESS_FORCE_SKIP_TERMINAL:-}" ]; then
    TERM_PROG=""
elif command -v foot >/dev/null 2>&1; then
    TERM_PROG=foot
elif command -v kitty >/dev/null 2>&1; then
    TERM_PROG=kitty
elif command -v alacritty >/dev/null 2>&1; then
    TERM_PROG=alacritty
    ALACRITTY_TEST_CONF="$(mktemp /tmp/havel-alacritty-XXXXXX.toml)"
    printf '[colors.primary]\nbackground = "#242424"\n' > "$ALACRITTY_TEST_CONF"
fi
if [ -z "$TERM_PROG" ]; then
    skip "no terminal client (need foot, kitty or alacritty)"
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
if [ "$MAP_SEEN" -ne 0 ]; then
    # One retry: a client can lose the race against a loaded box (no map
    # in 45s is not "the compositor does not map windows"). Respawn once,
    # then judge.
    echo "note: no map in 45s, respawning the client once"
    kill "$FOOT_PID" 2>/dev/null
    case "$TERM_PROG" in
        foot)
            WAYLAND_DISPLAY=wayland-0 timeout 60 foot -e sh -c 'sleep 300' > /dev/null 2>&1 & ;;
        kitty)
            WAYLAND_DISPLAY=wayland-0 timeout 60 kitty -o background=#242424 -o background_opacity=1 -e sh -c 'sleep 300' > /dev/null 2>&1 & ;;
        alacritty)
            WAYLAND_DISPLAY=wayland-0 timeout 60 alacritty --config-file "$ALACRITTY_TEST_CONF" -e sh -c 'sleep 300' > /dev/null 2>&1 & ;;
    esac
    FOOT_PID=$!
    for _ in $(seq 1 45); do
        grep -q "onViewMapped" "$LOG" && { MAP_SEEN=0; break; }
        sleep 1
    done
fi
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
if HAVEL_IPC_SOCKET="$SOCK" python3 tests/runtime/ipc_probe.py > /tmp/havel-ipc-out.txt 2>&1; then
    echo "PASS: IPC surface (28 valid + 6 hostile + 4 havel_run)"; PASS=$((PASS+1))
else
    echo "FAIL: IPC probe"; cat /tmp/havel-ipc-out.txt | tail -8
    FAIL=$((FAIL+1))
fi
if HAVEL_IPC_SOCKET="$SOCK" python3 tests/runtime/compositing_probe.py > /tmp/havel-compositing-out.txt 2>&1; then
    echo "PASS: compositing IPC state round-trip"; PASS=$((PASS+1))
else
    echo "FAIL: compositing IPC probe"; cat /tmp/havel-compositing-out.txt | tail -8
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

    # Meta+Shift+P toggles forced scene composition. Query the real IPC
    # state after each injection so this checks the key path, not a log line.
    WAYLAND_DISPLAY=wayland-0 "$INJ" +125 +42 25 -42 -125
    report $(HAVEL_IPC_SOCKET="$SOCK" python3 tests/runtime/compositing_probe.py --expect-compositing false >/tmp/havel-compositing-key-out.txt 2>&1 && echo 0 || echo 1) "Meta+Shift+P disables compositing"
    WAYLAND_DISPLAY=wayland-0 "$INJ" +125 +42 25 -42 -125
    report $(HAVEL_IPC_SOCKET="$SOCK" python3 tests/runtime/compositing_probe.py --expect-compositing true >/tmp/havel-compositing-key-out.txt 2>&1 && echo 0 || echo 1) "Meta+Shift+P restores compositing"
else
    skip "injector build failed (wayland-scanner missing?)"
fi

# ---------------------------------------------------------------- phase 6
echo "--- phase 6: havel language-syntax hotkeys (config.hv)"
if [ "${HAS_EMBED:-0}" -eq 0 ]; then
    skip "compositor built without the havel embed (no [Havel] line at boot)"
else
# The compositing host functions must drive the same compositor state as
# the IPC commands and the built-in keybinding.
if HAVEL_IPC_SOCKET="$SOCK" python3 tests/runtime/compositing_probe.py --via-havel >/tmp/havel-compositing-havel-out.txt 2>&1; then
    report 0 "Havel set_compositing/toggle_compositing state round-trip"
else
    report 1 "Havel compositing host functions"
    cat /tmp/havel-compositing-havel-out.txt | tail -8
fi

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
PARITY_IPC1="/run/user/$(id -u)/havel-wm-parity1.sock"
rm -f "$PARITY_IPC1"
HOME="$PARITY_HOME" HAVEL_IPC_SOCKET="$PARITY_IPC1" \
    WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
    timeout 900 "$BUILD_DIR/bin/havel-wm" > /tmp/havel-parity-wm.log 2>&1 &
PARITY_PID=$!
OLD_LOG="$LOG"; LOG=/tmp/havel-parity-wm.log
# The main harness WM still holds wayland-0; the parity instance
# auto-increments. The real readiness signal: the parity WM's own
# "Compositor running on <sock>" line (config.hv loaded by then). Wait
# for it, then read the actual socket name from the log - never assume.
PARITY_SOCK=""
for _ in $(seq 2000); do
    if grep -q "Compositor running on" /tmp/havel-parity-wm.log 2>/dev/null; then
        PARITY_SOCK=$(grep -o "Compositor running on wayland-[0-9]*" /tmp/havel-parity-wm.log | head -1 | grep -o "wayland-[0-9]*")
        break
    fi
    sleep 0.2
done
if [ -z "$PARITY_SOCK" ]; then
    skip "parity compositor never became ready"
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
        timeout 900 "$BUILD_DIR/bin/havel-wm" > /tmp/havel-parity-wm2.log 2>&1 &
    PARITY_PID=$!
    LOG=/tmp/havel-parity-wm2.log
    P2_SOCK=""
    for _ in $(seq 2000); do
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
        skip "parity restart never became ready"
    fi
    report $([ "$(crashes)" -eq 0 ] && echo 0 || echo 1) "parity compositor clean"
fi   # closes: if [ -z "$PARITY_SOCK" ]
fi   # HAS_EMBED gate (phase 6)
kill "${PARITY_PID:-}" 2>/dev/null
[ -n "${PARITY_HOME:-}" ] && rm -rf "$PARITY_HOME"
LOG="${OLD_LOG:-$LOG}"

# ---------------------------------------------------------------- phase 7
echo "--- phase 7: alt-tab grid overlay"
if [ ! -x "$INJ" ]; then
    skip "key injector not built"
elif [ -z "$TERM_PROG" ]; then
    skip "no terminal client for switcher windows"
else
    # The switcher needs windows to list; earlier phases closed theirs.
    MAPS_BEFORE=$(grep -acE "View mapped=true" "$LOG")
    GRID_PIDS=""
    for _ in 1 2; do
        WAYLAND_DISPLAY=wayland-0 timeout 110 "$TERM_PROG" -e sh -c 'sleep 100' > /dev/null 2>&1 &
        GRID_PIDS="$GRID_PIDS $!"
        sleep 1
    done
    EMAP=1
    for _ in $(seq 1 150); do
        if [ "$(grep -acE 'View mapped=true' "$LOG")" -ge $((MAPS_BEFORE + 2)) ]; then
            EMAP=0; break
        fi
        sleep 0.2
    done
    report "$EMAP" "two windows mapped for the switcher"

    # Thumbnail probe: a third window with a distinctive solid background.
    # Its cell then shows the captured content (red pixels inside the
    # grid); before the capture path existed, cells were flat
    # placeholders. wlred (native wl_shm client, no fonts/GL) goes first:
    # the terminal paths were races under load - alacritty's EGL/fontconfig
    # boot frequently missed the map window and the check skipped. kitty's
    # EGL/ZINK init fails headless in this environment, and this foot build
    # rejects [colors] from a config file (version-dependent), so both stay
    # as fallbacks that may end in an honest skip.
    THUMB_RED=1
    REDPROBE="$BUILD_DIR/tests/wlred"
    if [ ! -x "$REDPROBE" ]; then
        mkdir -p "$BUILD_DIR/tests"
        XDG_XML="$(pkg-config --variable=pkgdatadir wayland-protocols 2>/dev/null)/stable/xdg-shell/xdg-shell.xml"
        if [ -f "$XDG_XML" ]; then
            wayland-scanner private-code "$XDG_XML" "$BUILD_DIR/tests/xdg-shell-protocol.c"
            wayland-scanner client-header "$XDG_XML" "$BUILD_DIR/tests/xdg-shell-client-protocol.h"
            cc -o "$REDPROBE" tests/runtime/wlred.c "$BUILD_DIR/tests/xdg-shell-protocol.c" \
                $(pkg-config --cflags --libs wayland-client) -I"$BUILD_DIR/tests" || true
        fi
    fi
    if [ -x "$REDPROBE" ]; then
        # daemonize: the probe owns its buffers and stays alive until
        # killed by the harness. A bare `&` made the probe a child of the
        # phase-7 shell, so the shell exiting (or the harness waiting on
        # the shared injection PID) reaped it mid-capture - the window
        # mapped and DESTROYed within milliseconds under load, leaving
        # the red cell empty (observed: 'Removing window 5: wlred-
        # thumb-test' 3ms after 'App ID set').
        setsid "$REDPROBE" </dev/null >/dev/null 2>&1 &
        GRID_PIDS="$GRID_PIDS $!"
        PROBE_DAEMONIZED=1
    elif command -v alacritty >/dev/null 2>&1; then
        THUMB_CONF="$(mktemp /tmp/havel-alacred-XXXXXX.toml)"
        printf '[colors.primary]\nbackground = "#ff0000"\n' > "$THUMB_CONF"
        WAYLAND_DISPLAY=wayland-0 timeout 110 alacritty --config-file "$THUMB_CONF" \
            -e sh -c 'sleep 100' > /dev/null 2>&1 &
        GRID_PIDS="$GRID_PIDS $!"
    elif command -v kitty >/dev/null 2>&1; then
        WAYLAND_DISPLAY=wayland-0 LIBGL_ALWAYS_SOFTWARE=1 timeout 110 kitty \
            -o background=#ff0000 -o background_opacity=1 \
            -e sh -c 'sleep 100' > /dev/null 2>&1 &
        GRID_PIDS="$GRID_PIDS $!"
    else
        THUMB_RED=0
    fi
    if [ "$THUMB_RED" = "1" ]; then
        for _ in $(seq 1 100); do
            if [ "$(grep -acE 'View mapped=true' "$LOG")" -ge $((MAPS_BEFORE + 3)) ]; then
                break
            fi
            sleep 0.2
        done
        if [ "$(grep -acE 'View mapped=true' "$LOG")" -lt $((MAPS_BEFORE + 3)) ]; then
            THUMB_RED=0   # probe window never mapped: skip the colour check
        fi
    fi

    # Hold Alt, tap Tab, capture while it is held, release Alt - all on
    # ONE virtual keyboard: modifier state is per keyboard, so a second
    # injector would tap Tab without the Alt modifier. The capture polls
    # and keeps the best overlay frame: a fixed 1.0s sleep caught partial
    # fade-ins on a loaded box (panel counted ~1.2k there vs ~90k when
    # drawn, observed repeatedly).
    #
    # The hold is sized to the harness's own slow steps, not to the
    # overlay: on a loaded box the injector's setup takes seconds and each
    # grim+probe round trip ~1-2s, so a 5s hold expired before any capture
    # saw the overlay (measured: the drawn overlay first appeared on the
    # 5th capture, ~4s in). 12s covers that with margin while keeping the
    # post-poll wait (the release that proves the grid clears) bounded.
    WAYLAND_DISPLAY=wayland-0 timeout 40 "$INJ" +56 15 w12000 -56 > /dev/null 2>&1 &
    INJ_PID=$!
    GRID_UP="$(mktemp /tmp/havel-grid-up-XXXXXX.png)"
    GRID_TMP="$(mktemp /tmp/havel-grid-poll-XXXXXX.png)"
    # Settle condition spans BOTH layers the checks measure: the panel is
    # drawn before the full-screen dim finishes fading, so breaking on the
    # panel alone captured a frame whose dim count landed a hair under its
    # threshold (dim=45995 vs 46080, missed by 0.18%, while panel/cells/
    # selected all passed comfortably - observed). Keep the capture with
    # the highest combined score and only stop once both are well clear.
    GRID_BEST=0
    for _ in $(seq 1 30); do
        WAYLAND_DISPLAY=wayland-0 grim "$GRID_TMP" 2>/dev/null || { sleep 0.3; continue; }
        read -r PP DD <<EOF
$(python3 - "$GRID_TMP" <<'PPPY' 2>/dev/null || echo "0 0"
import sys
from PIL import Image
from collections import Counter
im = Image.open(sys.argv[1]).convert('RGB')
px = im.load(); w, h = im.size
c = Counter(px[x, y] for x in range(0, w, 2) for y in range(0, h, 2))
def near(t, tol=6):
    return sum(n for k, n in c.items() if all(abs(k[i] - t[i]) <= tol for i in range(3)))
print(near((33, 33, 43)), near((7, 7, 11)))
PPPY
)
EOF
        SCORE=$(( ${PP:-0} + ${DD:-0} ))
        if [ "$SCORE" -gt "$GRID_BEST" ]; then
            GRID_BEST="$SCORE"
            cp "$GRID_TMP" "$GRID_UP" 2>/dev/null
        fi
        # Both layers clear of their thresholds (panel 2304, dim 46080):
        # wait for the animation to finish rather than catching it mid-way.
        if [ "${PP:-0}" -ge 30000 ] && [ "${DD:-0}" -ge 60000 ]; then break; fi
        sleep 0.3
    done
    rm -f "$GRID_TMP"
    wait "$INJ_PID" 2>/dev/null
    # The overlay must clear once the modifier lifts. Poll instead of a
    # single fixed delay: the scene redraw after the node destroy lands
    # within a frame or two, but the harness box is loaded.
    GRID_DOWN="$(mktemp /tmp/havel-grid-down-XXXXXX.png)"
    for _ in $(seq 1 10); do
        WAYLAND_DISPLAY=wayland-0 grim "$GRID_DOWN" 2>/dev/null || { sleep 0.5; continue; }
        CLEAR=$(python3 - "$GRID_DOWN" <<'CLEAR_PY'
import sys
from PIL import Image
from collections import Counter
try:
    im = Image.open(sys.argv[1]).convert('RGB')
    px = im.load(); w, h = im.size
    c = Counter(px[x, y] for x in range(0, w, 3) for y in range(0, h, 3))
    panel = c.get((33, 33, 43), 0) + c.get((64, 82, 108), 0)
    print(1 if panel < 50 else 0)
except Exception:
    print(0)
CLEAR_PY
)
        [ "${CLEAR:-0}" -eq 1 ] && break
        sleep 0.5
    done

    GRID_STATS=$(python3 - "$GRID_UP" "$GRID_DOWN" <<'GRID_PY'
import sys
from PIL import Image
from collections import Counter
up, down = sys.argv[1], sys.argv[2]
def census(f):
    im = Image.open(f).convert('RGB')
    px = im.load(); w, h = im.size
    c = Counter(px[x, y] for x in range(0, w, 2) for y in range(0, h, 2))
    return c, (w // 2) * (h // 2)
def near(c, target, tol=6):
    return sum(n for k, n in c.items() if all(abs(k[i] - target[i]) <= tol for i in range(3)))
try:
    c, total = census(up)
except Exception as e:
    print("dim=0 panel=0 cell=0 sel=0 total=1 gone=0"); sys.exit(0)
dim   = near(c, (7, 7, 11))       # desktop under the 0.7 dim
panel = near(c, (33, 33, 43))     # overlay panel
cell  = near(c, (40, 40, 51))     # unselected cell
sel   = near(c, (64, 82, 108))    # selected cell
try:
    c2, _ = census(down)
    gone = 1 if (near(c2, (33, 33, 43)) < 50 and near(c2, (64, 82, 108)) < 50) else 0
except Exception:
    gone = 0
print("dim=%d panel=%d cell=%d sel=%d total=%d gone=%d" % (dim, panel, cell, sel, total, gone))
GRID_PY
)
    G() { echo "$GRID_STATS" | grep -o "$1=[0-9]*" | head -1 | cut -d= -f2; }
    TOTAL=$(G total); TOTAL=${TOTAL:-1}
    report $([ "$(G dim)"   -ge $((TOTAL / 5)) ] && echo 0 || echo 1) "overlay dimmed the desktop (dim=$(G dim)/$TOTAL)"
    report $([ "$(G panel)" -ge $((TOTAL / 100)) ] && echo 0 || echo 1) "switcher panel drawn (panel=$(G panel))"
    report $([ "$(G cell)"  -ge $((TOTAL / 200)) ] && echo 0 || echo 1) "switcher cells drawn (cells=$(G cell))"
    report $([ "$(G sel)"   -ge $((TOTAL / 200)) ] && echo 0 || echo 1) "selected cell highlighted (sel=$(G sel))"

    # Thumbnail: red pixels inside the grid area = the red window's
    # captured content showing in its cell (only when the probe window
    # was spawned). Grid area on 1280x800: panel x 64..1216, y 224..576.
    # REDPX: 0 unless the red probe window was spawned (see above)
    REDPX=0
    REDPX=$(python3 -c "
from PIL import Image
im = Image.open('$GRID_UP').convert('RGB'); px = im.load(); w, h = im.size
print(sum(1 for y in range(int(h*0.30), int(h*0.70), 2)
          for x in range(int(w*0.06), int(w*0.94), 2)
          if px[x, y][0] > 200 and px[x, y][1] < 80 and px[x, y][2] < 80))" 2>/dev/null || echo 0)
    if [ "$THUMB_RED" -eq 1 ]; then
        report $([ "${REDPX:-0}" -gt 300 ] && echo 0 || echo 1) "window thumbnail rendered in cell (red px: ${REDPX:-err})"
    else
        skip "window thumbnail colour check (no red probe window mapped)"
    fi
    # Cell header icon: the switcher draws each entry's app icon at the
    # cell's top-left (28px at +8,+8). STRONG evidence: the probe's app
    # id is "wlred" - an id with no icon anywhere on disk - so the loader
    # MUST fall back to the hash placeholder, which is a SOLID block of
    # one colour. Count pixels inside the icon square that differ from
    # the cell background: a drawn placeholder yields hundreds, a skipped
    # icon path yields the cell background only.
    ICONPX=$(python3 -c "
from PIL import Image
from collections import Counter
im = Image.open('$GRID_UP').convert('RGB'); px = im.load(); w, h = im.size
# the grid's first cell sits in the upper-left of the overlay; scan a
# generous band and take the most common non-background colour count.
c = Counter()
for y in range(int(h*0.28), int(h*0.62), 2):
    for x in range(int(w*0.08), int(w*0.34), 2):
        p = px[x, y]
        if p[0] < 12 and p[1] < 12 and p[2] < 16: continue      # near black
        if abs(p[0]-33) <= 8 and abs(p[1]-33) <= 8 and abs(p[2]-43) <= 10: continue  # panel bg
        if abs(p[0]-64) <= 10 and abs(p[1]-82) <= 10 and abs(p[2]-108) <= 12: continue  # selected cell
        c[p] += 1
print(c.most_common(1)[0][1] if c else 0)" 2>/dev/null || echo 0)
    if [ "$THUMB_RED" -eq 1 ]; then
        report $([ "${ICONPX:-0}" -gt 40 ] && echo 0 || echo 1) "switcher cell icon drawn (icon px: ${ICONPX:-err})"
    else
        skip "switcher cell icon check (no red probe window mapped)"
    fi
    [ -n "${THUMB_CONF:-}" ] && rm -f "$THUMB_CONF"
    DOWNPANEL=$(python3 - "$GRID_DOWN" <<'DOWN_PY'
import sys
from PIL import Image
from collections import Counter
try:
    im = Image.open(sys.argv[1]).convert('RGB'); px = im.load(); w, h = im.size
    c = Counter(px[x, y] for x in range(0, w, 3) for y in range(0, h, 3))
    print(c.get((33, 33, 43), 0) + c.get((64, 82, 108), 0))
except Exception:
    print(-1)
DOWN_PY
)
    # CLEAR is the poll result (the capture right after the release can
    # still show the last frame); it is the authoritative check.
    report $([ "${CLEAR:-0}" -eq 1 ] && echo 0 || echo 1) "grid cleared after the modifier lifts (down_panel=$DOWNPANEL)"
    rm -f "$GRID_UP" "$GRID_DOWN"
    kill $GRID_PIDS 2>/dev/null
fi

# ---------------------------------------------------------------- phase 8
echo "--- phase 8: wallpaper behind windows"
# The wallpaper used to be unreachable (no command, no renderer call) -
# this phase exercises the IPC path end to end with its own pattern, so it
# does not depend on files in $HOME.
WPIMG="$(mktemp /tmp/havel-wp-XXXXXX.png)"
python3 - "$WPIMG" <<'WP_PY'
import sys
from PIL import Image
im = Image.new('RGB', (320, 200))
px = im.load()
for y in range(200):
    for x in range(320):
        px[x, y] = (255, 0, 255) if (x // 40 + y // 40) % 2 == 0 else (0, 255, 0)
im.save(sys.argv[1])
WP_PY
if python3 - "$WPIMG" "$SOCK" <<'WPSET' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[2])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"set_wallpaper",
                       "params":{"path":sys.argv[1]}})+"\n").encode())
r = json.loads(s.recv(65536))
raise SystemExit(0 if r.get("result",{}).get("success") else 1)
WPSET
then
    sleep 0.8
    WPCAP="$(mktemp /tmp/havel-wp-cap-XXXXXX.png)"
    WAYLAND_DISPLAY=wayland-0 grim "$WPCAP" 2>/dev/null
    WPCOUNTS=$(python3 - "$WPCAP" <<'WP_COUNT'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); px = im.load(); w, h = im.size
mg = sum(1 for y in range(0, h, 4) for x in range(0, w, 4) if px[x, y] == (255, 0, 255))
gr = sum(1 for y in range(0, h, 4) for x in range(0, w, 4) if px[x, y] == (0, 255, 0))
print("%d %d" % (mg, gr))
WP_COUNT
)
    WPMG=$(echo "$WPCOUNTS" | awk '{print $1}')
    WPGR=$(echo "$WPCOUNTS" | awk '{print $2}')
    TOTAL=$(( (1280 / 4) * (720 / 4) ))
    report $([ "${WPMG:-0}" -gt 1000 ] && [ "${WPGR:-0}" -gt 1000 ] && echo 0 || echo 1) \
        "wallpaper pattern drawn (magenta=${WPMG:-err} green=${WPGR:-err})"
    # clear and confirm the pattern is gone
    python3 - "$SOCK" <<'WPCLR' >/dev/null 2>&1
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"set_wallpaper",
                       "params":{"path":""}})+"\n").encode())
s.recv(65536)
WPCLR
    sleep 0.8
    WAYLAND_DISPLAY=wayland-0 grim "$WPCAP" 2>/dev/null
    WPCOUNTS2=$(python3 - "$WPCAP" <<'WP_COUNT2'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); px = im.load(); w, h = im.size
mg = sum(1 for y in range(0, h, 4) for x in range(0, w, 4) if px[x, y] == (255, 0, 255))
gr = sum(1 for y in range(0, h, 4) for x in range(0, w, 4) if px[x, y] == (0, 255, 0))
print("%d %d" % (mg, gr))
WP_COUNT2
)
    WPMG2=$(echo "$WPCOUNTS2" | awk '{print $1}')
    WPGR2=$(echo "$WPCOUNTS2" | awk '{print $2}')
    report $([ "${WPMG2:-1}" -lt 50 ] && [ "${WPGR2:-1}" -lt 50 ] && echo 0 || echo 1) \
        "wallpaper cleared (magenta=${WPMG2:-err} green=${WPGR2:-err})"
    rm -f "$WPCAP"
else
    skip "wallpaper phase (set_wallpaper failed)"
fi
rm -f "$WPIMG"

echo "--- phase 9: zoom"
# ---------------------------------------------------------------- phase 9: zoom
# Fullscreen zoom is the C-side scale + cursor-centred offset; lens/strip/
# custom are magnifier overlays fed by an in-process scene capture. Both
# paths get pixel evidence here against a two-colour test wallpaper:
# a control capture (mixed colours where the colour boundary crosses the
# sample box), the lens box turning solid after an 8x lens over a solid
# region, and the colour boundary moving right under a 2x fullscreen zoom.
ZWPIMG="$(mktemp /tmp/havel-zwp-XXXXXX.png)"
ZCAP1="$(mktemp /tmp/havel-zc1-XXXXXX.png)"
ZCAP2="$(mktemp /tmp/havel-zc2-XXXXXX.png)"
ZCAP3="$(mktemp /tmp/havel-zc3-XXXXXX.png)"
python3 - "$ZWPIMG" <<'ZWP_PY'
import sys
from PIL import Image
im = Image.new('RGB', (1280, 720))
px = im.load()
for y in range(720):
    for x in range(1280):
        px[x, y] = (255, 0, 255) if x < 640 else (0, 255, 0)
im.save(sys.argv[1])
ZWP_PY

zoom_ipc() {
    python3 - "$SOCK" "$1" <<'ZIPC' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"zoom",
                       "params":json.loads(sys.argv[2])})+"\n").encode())
r = json.loads(s.recv(65536))
print(json.dumps(r.get("result", r.get("error", {}))))
ZIPC
}

# park every client window in a fixed floating spot and drop back to
# workspace 0 - phase 5 spawns an interactive terminal that would
# otherwise cover the sample regions (get_windows returns a bare array;
# the spawned terminal lives on the workspace the key phase switched to)
PARKED=$(python3 - "$SOCK" <<'FPARK' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
def rpc(m, p):
    s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":m,"params":p})+"\n").encode())
    s.recv(65536)
rpc("workspace", {"id": 0})
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"get_windows","params":{}})+"\n").encode())
r = json.loads(s.recv(65536))
wins = r.get("result", [])
if isinstance(wins, dict): wins = wins.get("windows", [])
parked = 0
for w in wins:
    wid = w.get("id")
    if wid is None: continue
    rpc("set_floating", {"id": wid, "floating": True})
    rpc("move", {"id": wid, "x": 900, "y": 380})
    rpc("resize", {"id": wid, "w": 300, "h": 200})
    parked += 1
print(parked if len(wins) == parked else -1)
FPARK
)
sleep 0.5
if [ "${PARKED:--1}" -lt 0 ]; then
    report 1 "test clients parked, workspace 0 (parked=${PARKED:-err})"
elif [ "${PARKED:-0}" -eq 0 ]; then
    skip "test clients parked, workspace 0 (no clients alive to park)"
else
    report 0 "test clients parked, workspace 0 (parked=${PARKED:-err})"
fi

if python3 - "$SOCK" "$ZWPIMG" <<'ZWSET' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"set_wallpaper",
                       "params":{"path":sys.argv[2]}})+"\n").encode())
r = json.loads(s.recv(65536))
raise SystemExit(0 if r.get("result",{}).get("success") else 1)
ZWSET
then
    sleep 0.8

    # -- status round-trip: mode switch lands in the compositor state
    ZSTAT=$(zoom_ipc '{"action":"mode","mode":"lens"}')
    ZSTAT=$(zoom_ipc '{"action":"status"}')
    report $(echo "$ZSTAT" | grep -q '"mode": *"lens"' && echo 0 || echo 1) \
        "zoom status reports lens mode (${ZSTAT:0:60})"

    # -- control capture: the sample box straddles the colour boundary
    WAYLAND_DISPLAY=wayland-0 grim "$ZCAP1" 2>/dev/null
    ZCTRL=$(python3 - "$ZCAP1" <<'ZCTRL_PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); px = im.load()
mg = gr = 0
for y in range(310, 490, 2):
    for x in range(550, 730, 2):
        c = px[x, y]
        if c == (255, 0, 255): mg += 1
        elif c == (0, 255, 0): gr += 1
print("%d %d" % (mg, gr))
ZCTRL_PY
)
    ZCMG=$(echo "$ZCTRL" | awk '{print $1}'); ZCGR=$(echo "$ZCTRL" | awk '{print $2}')
    report $([ "${ZCMG:-0}" -gt 500 ] && [ "${ZCGR:-0}" -gt 500 ] && echo 0 || echo 1) \
        "zoom control box mixed (magenta=${ZCMG:-err} green=${ZCGR:-err})"

    # -- lens: 8x over the solid magenta region turns the box solid
    #    (mode is already lens from the status round-trip; one toggle arms it)
    zoom_ipc '{"action":"configure","toggle_level":8.0}' >/dev/null
    zoom_ipc '{"action":"toggle","output":0}' >/dev/null
    python3 - "$SOCK" <<'ZWARPa' >/dev/null 2>&1
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"warp_cursor",
                       "params":{"x":600,"y":360}})+"\n").encode())
s.recv(65536)
ZWARPa
    sleep 1.5
    ZSTAT2=$(zoom_ipc '{"action":"status"}')
    report $(echo "$ZSTAT2" | grep -q '"mag": *8' && echo 0 || echo 1) \
        "zoom lens target applied (mag 8)"
    WAYLAND_DISPLAY=wayland-0 grim "$ZCAP2" 2>/dev/null
    ZLENS=$(python3 - "$ZCAP2" <<'ZLENS_PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); px = im.load()
mg = tot = 0
for y in range(310, 490, 2):
    for x in range(550, 730, 2):
        c = px[x, y]
        tot += 1
        if c == (255, 0, 255): mg += 1
print("%d %d" % (mg, tot))
ZLENS_PY
)
    ZLMG=$(echo "$ZLENS" | awk '{print $1}'); ZLTOT=$(echo "$ZLENS" | awk '{print $2}')
    report $([ "${ZLTOT:-1}" -gt 0 ] && [ $(( ${ZLMG:-0} * 10 )) -ge $(( ${ZLTOT:-1} * 9 )) ] && echo 0 || echo 1) \
        "lens box solid at 8x (magenta=${ZLMG:-err}/${ZLTOT:-err})"

    # -- fullscreen: 2x zoom pushes the colour boundary right (640 -> ~680
    #    with the cursor at 600: pos = 600*(1-1/2) = 300, lx = (640-300)*2)
    zoom_ipc '{"action":"mode","mode":"fullscreen"}' >/dev/null
    zoom_ipc '{"action":"configure","toggle_level":2.0}' >/dev/null
    zoom_ipc '{"action":"toggle","output":0}' >/dev/null
    sleep 1.2
    WAYLAND_DISPLAY=wayland-0 grim "$ZCAP3" 2>/dev/null
    boundary_x() {
        python3 - "$1" <<'ZB_PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); px = im.load()
run = 0
for x in range(250, 1270):
    if px[x, 650] == (0, 255, 0):
        run += 1
        if run >= 5:
            print(x - 4); break
    else:
        run = 0
else:
    print(-1)
ZB_PY
    }
    ZB1=$(boundary_x "$ZCAP1"); ZB2=$(boundary_x "$ZCAP3")
    report $([ "${ZB1:-0}" -gt 400 ] && [ "${ZB2:-0}" -gt 0 ] && [ $((ZB2 - ZB1)) -gt 20 ] && [ $((ZB2 - ZB1)) -lt 160 ] && echo 0 || echo 1) \
        "fullscreen 2x moves boundary right (${ZB1:-err} -> ${ZB2:-err})"

    # -- cleanup: back to normal, wallpaper cleared
    zoom_ipc '{"action":"mode","mode":"fullscreen"}' >/dev/null
    zoom_ipc '{"action":"reset","output":"all"}' >/dev/null
    python3 - "$SOCK" <<'ZWCLR2' >/dev/null 2>&1
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"set_wallpaper",
                       "params":{"path":""}})+"\n").encode())
s.recv(65536)
ZWCLR2
    sleep 0.5
    ZCAP4="$(mktemp /tmp/havel-zc4-XXXXXX.png)"
    WAYLAND_DISPLAY=wayland-0 grim "$ZCAP4" 2>/dev/null
    ZAFTER=$(python3 - "$ZCAP4" <<'ZAFTER_PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); px = im.load()
mg = 0
for y in range(0, 720, 8):
    for x in range(0, 1280, 8):
        if px[x, y] == (255, 0, 255): mg += 1
print(mg)
ZAFTER_PY
)
    report $([ "${ZAFTER:-1}" -lt 200 ] && echo 0 || echo 1) \
        "zoom reset + wallpaper cleared (magenta=${ZAFTER:-err})"
    rm -f "$ZCAP4"
else
    skip "zoom phase (set_wallpaper failed)"
fi
rm -f "$ZWPIMG" "$ZCAP1" "$ZCAP2" "$ZCAP3"

# ---------------------------------------------------------------- phase 10: app launcher
# The launcher used to scan 0 applications (every .desktop file was
# preset noDisplay=true and most files never reset it). This phase opens
# the launcher over IPC, checks entries were scanned and the panel draws,
# types a search term through the virtual keyboard, and closes it again.
LAUNCHER_CAP="$(mktemp /tmp/havel-launcher-XXXXXX.png)"
ZCAP_L2="$(mktemp /tmp/havel-launcher2-XXXXXX.png)"
if plugin_result=$(python3 - "$SOCK" <<'LOPEN' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"plugin_command",
                       "params":{"plugin":"app_launcher","cmd":"open"}})+"\n").encode())
r = json.loads(s.recv(65536))
print("ok" if r.get("result", {}).get("success") else "fail")
LOPEN
) && [ "$plugin_result" = "ok" ]; then
    # Poll until the entry rows are drawn and keep the strongest frame. A
    # fixed 1.0s wait captured a partially drawn list on a loaded box (the
    # "entries listed" text count landed at 153-155 against a 200
    # threshold, while the same check reads ~450 once the rows are up).
    LTMP="$(mktemp /tmp/havel-launcher-poll-XXXXXX.png)"
    LBEST=0
    for _ in $(seq 1 25); do
        WAYLAND_DISPLAY=wayland-0 grim "$LTMP" 2>/dev/null || { sleep 0.3; continue; }
        read -r LP LT <<EOF
$(python3 - "$LTMP" <<'LPOLL' 2>/dev/null || echo "0 0"
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); px = im.load(); w, h = im.size
panel = sum(1 for y in range(h//2 - 190, h//2 + 190, 4)
            for x in range(w//2 - 290, w//2 + 290, 4)
            if 32 <= px[x, y][0] <= 44 and 32 <= px[x, y][1] <= 44 and 45 <= px[x, y][2] <= 57)
text = sum(1 for y in range(h//2 - 100, h//2 + 150, 2)
           for x in range(w//2 - 280, w//2 + 280, 2)
           if px[x, y][0] > 150 and px[x, y][1] > 150)
print(panel, text)
LPOLL
)
EOF
        if [ $(( ${LP:-0} + ${LT:-0} )) -gt "$LBEST" ]; then
            LBEST=$(( ${LP:-0} + ${LT:-0} ))
            cp "$LTMP" "$LAUNCHER_CAP" 2>/dev/null
        fi
        # Comfortably clear of both thresholds (panel 500, text 200).
        if [ "${LP:-0}" -ge 900 ] && [ "${LT:-0}" -ge 300 ]; then break; fi
        sleep 0.3
    done
    rm -f "$LTMP"
    LSTATS=$(python3 - "$LAUNCHER_CAP" <<'LSTATS_PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); px = im.load(); w, h = im.size
# panel: exact color (0.15, 0.15, 0.2) over the whole 600x400 area
panel = sum(1 for y in range(h//2 - 190, h//2 + 190, 4)
            for x in range(w//2 - 290, w//2 + 290, 4)
            if 32 <= px[x, y][0] <= 44 and 32 <= px[x, y][1] <= 44 and 45 <= px[x, y][2] <= 57)
# text pixels inside the result area = entry names drawn. The launcher
# renders non-selected rows at 0.8 gray; antialias coverage blends many
# core pixels below 200, so count the antialiased text too rather than
# only the brightest glyph cores.
text = sum(1 for y in range(h//2 - 100, h//2 + 150, 2)
           for x in range(w//2 - 280, w//2 + 280, 2)
           if px[x, y][0] > 150 and px[x, y][1] > 150)
print("%d %d" % (panel, text))
LSTATS_PY
)
    LPANEL=$(echo "$LSTATS" | awk '{print $1}')
    LTEXT=$(echo "$LSTATS" | awk '{print $2}')
    report $([ "${LPANEL:-0}" -gt 500 ] && echo 0 || echo 1) \
        "launcher panel drawn (panel=${LPANEL:-err})"
    report $([ "${LTEXT:-0}" -gt 200 ] && echo 0 || echo 1) \
        "launcher entries listed (text px=${LTEXT:-err})"

    # close via command; verify the overlay is gone
    python3 - "$SOCK" <<'LCLOSE' >/dev/null 2>&1
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"plugin_command",
                       "params":{"plugin":"app_launcher","cmd":"close"}})+"\n").encode())
s.recv(65536)
LCLOSE
    sleep 0.6
    WAYLAND_DISPLAY=wayland-0 grim "$ZCAP_L2" 2>/dev/null
    LAFTER=$(python3 - "$ZCAP_L2" <<'LAFTER_PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); px = im.load(); w, h = im.size
panel = sum(1 for y in range(h//2 - 190, h//2 + 190, 4)
            for x in range(w//2 - 290, w//2 + 290, 4)
            if 32 <= px[x, y][0] <= 44 and 32 <= px[x, y][1] <= 44 and 45 <= px[x, y][2] <= 57)
print(panel)
LAFTER_PY
)
    report $([ "${LAFTER:-1}" -lt 50 ] && echo 0 || echo 1) \
        "launcher closed cleanly (panel=${LAFTER:-err})"
    rm -f "$LAUNCHER_CAP" "$ZCAP_L2"
else
    skip "launcher phase (plugin_command open failed)"
fi

# ---------------------------------------------------------------- phase 11: panel
# The panel (WorkspaceInfoBar v2) draws a launcher button, workspace
# buttons, window buttons and a clock per monitor, with a workarea
# reservation. Pixel checks: the launcher button block, the workspace
# button row (accent-tinted active cell), and the clock area text.
PANEL_CAP="$(mktemp /tmp/havel-panel-XXXXXX.png)"
sleep 1.2   # let the 1 Hz persistent tree rebuild with the new layout
WAYLAND_DISPLAY=wayland-0 grim "$PANEL_CAP" 2>/dev/null
PSTATS=$(python3 - "$PANEL_CAP" <<'PSTATS_PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); px = im.load(); w, h = im.size
def count(x0, x1, y0, y1, pred):
    return sum(1 for y in range(y0, y1, 2) for x in range(x0, x1, 2) if pred(px[x, y]))
# top strip of the panel; bar = 0.85 black over bg (0.1,0.1,0.15) -> ~(4,4,6)
panel_bg = count(0, w, h - 28, h, lambda c: c[0] < 15 and c[1] < 15 and c[2] < 25)
# launcher button: accent-tinted block at the far left
accent = count(2, 120, h - 26, h - 4, lambda c: c[2] > 120 and c[2] > c[0] + 40 and c[1] > 60)
# bright text pixels in the top strip (Apps, workspace numbers, clock)
text = count(0, w, h - 26, h - 4, lambda c: c[0] > 180 and c[1] > 180 and c[2] > 180)
print("%d %d %d" % (panel_bg, accent, text))
PSTATS_PY
)
PPANEL=$(echo "$PSTATS" | awk '{print $1}')
PACCENT=$(echo "$PSTATS" | awk '{print $2}')
PTEXT=$(echo "$PSTATS" | awk '{print $3}')
report $([ "${PPANEL:-0}" -gt 2000 ] && echo 0 || echo 1) \
    "panel strip drawn (bg=${PPANEL:-err})"
report $([ "${PACCENT:-0}" -gt 20 ] && echo 0 || echo 1) \
    "panel launcher button visible (accent=${PACCENT:-err})"
report $([ "${PTEXT:-0}" -gt 30 ] && echo 0 || echo 1) \
    "panel text (workspaces/clock) visible (text=${PTEXT:-err})"
rm -f "$PANEL_CAP"

# ---------------------------------------------------------------- phase 12: file manager IPC
# file_list / file_copy / file_delete / open_path against a temp dir with
# known content; the compositor-side FileManager finally gets exercised.
FILEDIR="$(mktemp -d /tmp/havel-files-XXXXXX)"
echo "alpha" > "$FILEDIR/a.txt"
echo "beta" > "$FILEDIR/b.txt"
mkdir -p "$FILEDIR/subdir"
file_ipc() {
    python3 - "$SOCK" "$1" "$2" <<'FIPC' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":sys.argv[2],
                       "params":json.loads(sys.argv[3])})+"\n").encode())
r = json.loads(s.recv(65536))
print(json.dumps(r.get("result", r.get("error", {}))))
FIPC
}
FLIST=$(file_ipc file_list "{\"path\":\"$FILEDIR\"}")
report $(echo "$FLIST" | grep -q '"count": 3' && echo "$FLIST" | grep -q '"a.txt"' && echo 0 || echo 1) \
    "file_list returns entries ($(echo "$FLIST" | head -c 60))"
FINFO=$(file_ipc file_info "{\"path\":\"$FILEDIR/a.txt\"}")
report $(echo "$FINFO" | grep -q '"mime": "text/plain"' && echo 0 || echo 1) \
    "file_info mime detection ($(echo "$FINFO" | head -c 60))"
FCOPY=$(file_ipc file_copy "{\"src\":\"$FILEDIR/a.txt\",\"dst\":\"$FILEDIR/copy.txt\"}")
report $(echo "$FCOPY" | grep -q '"success": true' && [ -f "$FILEDIR/copy.txt" ] && echo 0 || echo 1) \
    "file_copy creates the copy"
FDEL=$(file_ipc file_delete "{\"path\":\"$FILEDIR/copy.txt\"}")
report $(echo "$FDEL" | grep -q '"success": true' && [ ! -f "$FILEDIR/copy.txt" ] && echo 0 || echo 1) \
    "file_delete removes the copy"
FBAD=$(file_ipc file_delete "{\"path\":\"/etc/passwd\"}")
report $(echo "$FBAD" | grep -q 'must be under HOME or /tmp' && echo 0 || echo 1) \
    "file ops reject paths outside HOME//tmp ($(echo "$FBAD" | head -c 50))"
FOPEN=$(file_ipc open_path "{\"path\":\"$FILEDIR\"}")
report $(echo "$FOPEN" | grep -q '"success": true' && echo 0 || echo 1) \
    "open_path opens a directory ($(echo "$FOPEN" | head -c 60))"
rm -rf "$FILEDIR"
sleep 0.3

# ---------------------------------------------------------------- phase 13: settings store
# settings_set/get/list with live application: gamma routes to the display
# settings, plugin.* keys push into plugins.json and reload the plugin.
SSET() {
    python3 - "$SOCK" "$1" "$2" <<'SIPC' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"settings",
                       "params":json.loads(sys.argv[3])})+"\n").encode())
r = json.loads(s.recv(65536))
print(json.dumps(r.get("result", r.get("error", {}))))
SIPC
}
SGAMMA=$(SSET x "{\"action\":\"set\",\"key\":\"gamma\",\"value\":\"1.5\"}")
report $(echo "$SGAMMA" | grep -q '"success": true' && echo 0 || echo 1) \
    "settings_set gamma 1.5"
SGAMMA2=$(SSET x "{\"action\":\"get\",\"key\":\"gamma\"}")
report $(echo "$SGAMMA2" | grep -q '"value": "1.5"' && echo 0 || echo 1) \
    "settings_get returns gamma ($(echo "$SGAMMA2" | head -c 50))"
GDSP=$(python3 - "$SOCK" <<'GDSP' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"get_display_settings","params":{}})+"\n").encode())
print(s.recv(65536).decode())
GDSP
)
report $(echo "$GDSP" | grep -q '"gamma":1.5' && echo 0 || echo 1) \
    "gamma setting applied to display settings"

# plugin.* key: taller panel, pixel-verified (bar bg reaches y=38)
PANEL_CAP2="$(mktemp /tmp/havel-panel2-XXXXXX.png)"
SSET x "{\"action\":\"set\",\"key\":\"plugin.workspace_info_bar.height\",\"value\":\"44\"}" >/dev/null
sleep 0.6
WAYLAND_DISPLAY=wayland-0 grim "$PANEL_CAP2" 2>/dev/null
PDEEP=$(python3 - "$PANEL_CAP2" <<'PDEEP_PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); px = im.load(); w, h = im.size
# bar bg (0.85 black over bg) sampled in the zone the taller panel now
# owns: the bar is at the BOTTOM, so the extra 14px sits just above the
# screen bottom edge (h-44 .. h-30)
return_val = sum(1 for y in range(h - 44, h - 30, 2) for x in range(300, 900, 8)
                 if px[x, y][0] < 15 and px[x, y][1] < 15 and px[x, y][2] < 25)
print(return_val)
PDEEP_PY
)
report $([ "${PDEEP:-0}" -gt 50 ] && echo 0 || echo 1) \
    "plugin setting live-applied (panel taller, deep px=${PDEEP:-err})"
rm -f "$PANEL_CAP2"
# restore panel height so later phases see the default layout
SSET x "{\"action\":\"set\",\"key\":\"plugin.workspace_info_bar.height\",\"value\":\"30\"}" >/dev/null
SSET x "{\"action\":\"set\",\"key\":\"gamma\",\"value\":\"1.0\"}" >/dev/null

# ---------------------------------------------------------------- phase 14: reload_config
# Re-running config.hv must (a) drop the OLD script bindings - register
# refuses duplicate combos, so a plain re-run would disable every script
# hotkey - (b) execute the new file, and (c) report a broken config
# through IPC instead of dying. Own compositor instance (own HOME, own
# socket, same pattern as phase 6's parity WM).
if [ "${HAS_EMBED:-0}" -eq 0 ]; then
    skip "reload_config (no havel embed)"
else
    RL_HOME="$(mktemp -d /tmp/havel-reload-XXXXXX)"
    mkdir -p "$RL_HOME/.config/havel-wm"
    cat > "$RL_HOME/.config/havel-wm/config.hv" <<'EOF'
keybind("Meta+F9", fn() { log("harness-reload-old-fired") })
EOF
    RL_IPC="/run/user/$(id -u)/havel-wm-reload.sock"
    rm -f "$RL_IPC"
    HOME="$RL_HOME" HAVEL_IPC_SOCKET="$RL_IPC" \
        WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
        timeout 900 "$BUILD_DIR/bin/havel-wm" > /tmp/havel-reload-wm.log 2>&1 &
    RL_PID=$!
    OLD_LOG="$LOG"; LOG=/tmp/havel-reload-wm.log
    RL_WAYLAND=""
    for _ in $(seq 2000); do
        if grep -q "Compositor running on" /tmp/havel-reload-wm.log 2>/dev/null; then
            RL_WAYLAND=$(grep -o "Compositor running on wayland-[0-9]*" /tmp/havel-reload-wm.log | head -1 | grep -o "wayland-[0-9]*")
            break
        fi
        sleep 0.2
    done
    rl_ipc() {
        python3 - "$RL_IPC" "$1" "$2" <<'RIPCS' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":sys.argv[2],
                       "params":json.loads(sys.argv[3])})+"\n").encode())
r = json.loads(s.recv(65536))
print(json.dumps(r.get("result", r.get("error", {}))))
RIPCS
    }
    if [ -z "$RL_WAYLAND" ]; then
        skip "reload compositor never became ready"
    else
        # boot-time binding works without any reload
        WAYLAND_DISPLAY="$RL_WAYLAND" "$INJ" +125 67 -125   # Meta+F9
        sleep 0.4
        OLDHITS=$(grep -ac "harness-reload-old-fired" /tmp/havel-reload-wm.log)
        report $([ "${OLDHITS:-0}" -ge 1 ] && echo 0 || echo 1) \
            "boot binding fires (F9 hits=${OLDHITS:-err})"

        # step 1: rewrite the config to bind F8 instead; reload; F9 dies, F8 lives
        cat > "$RL_HOME/.config/havel-wm/config.hv" <<'EOF'
keybind("Meta+F8", fn() { log("harness-reload-new-fired") })
EOF
        RLC=$(rl_ipc reload_config '{}')
        report $(echo "$RLC" | grep -q '"success": true' && echo 0 || echo 1) \
            "reload_config succeeds ($(echo "$RLC" | head -c 50))"
        WAYLAND_DISPLAY="$RL_WAYLAND" "$INJ" +125 67 -125   # F9 (should be dead)
        WAYLAND_DISPLAY="$RL_WAYLAND" "$INJ" +125 66 -125   # F8 (new)
        sleep 0.5
        OLDHITS2=$(grep -ac "harness-reload-old-fired" /tmp/havel-reload-wm.log)
        NEWHITS=$(grep -ac "harness-reload-new-fired" /tmp/havel-reload-wm.log)
        report $([ "${OLDHITS2:-9}" -eq "${OLDHITS:-9}" ] && [ "${NEWHITS:-0}" -ge 1 ] && echo 0 || echo 1) \
            "reload swaps bindings (old hits still=${OLDHITS2:-err}, new=${NEWHITS:-err})"

        # step 2: a broken config is refused through IPC; bindings survive
        cat > "$RL_HOME/.config/havel-wm/config.hv" <<'EOF'
keybind("Meta+F7", fn() { this is not havel })
EOF
        BAD=$(rl_ipc reload_config '{}')
        report $(echo "$BAD" | grep -q '"success": true' && echo 1 || echo 0) \
            "broken config refused ($(echo "$BAD" | head -c 60))"
        WAYLAND_DISPLAY="$RL_WAYLAND" "$INJ" +125 66 -125   # F8 still works
        sleep 0.4
        NEWHITS2=$(grep -ac "harness-reload-new-fired" /tmp/havel-reload-wm.log)
        report $([ "${NEWHITS2:-0}" -ge 2 ] && echo 0 || echo 1) \
            "bindings survive failed reload (F8 hits=${NEWHITS2:-err})"
        report $([ "$(crashes)" -eq 0 ] && echo 0 || echo 1) "reload compositor clean"
    fi
    kill "$RL_PID" 2>/dev/null
    [ -n "$RL_PID" ] && wait "$RL_PID" 2>/dev/null
    rm -rf "$RL_HOME"
    LOG="$OLD_LOG"
fi

# ---------------------------------------------------------------- phase 15: repl + introspection
# The interactive interpreter (havel-repl) evaluates through havel_eval
# and the C-API string handling is exercised end to end: host-function
# string arguments and host string returns (the old C-API resolved them
# through a colliding side table - settings("K","V") once stored a
# stale zoom-status JSON as the key). Introspection: get_outputs and
# get_keybindings answer with real state.
REPL_BIN="$BUILD_DIR/src/shell/repl/havel-repl"
if [ ! -x "$REPL_BIN" ]; then
    skip "repl evaluation (havel-repl not built)"
elif [ "${HAS_EMBED:-0}" -eq 0 ]; then
    skip "repl evaluation (no havel embed)"
else
    REPL_OUT=$(printf '1 + 2\nsettings("harness_repl_k", "harness_repl_v")\nsettings("harness_repl_k")\n:quit\n' \
        | timeout 20 env HAVEL_IPC_SOCKET="$SOCK" "$REPL_BIN" 2>/dev/null | sed 's/^havel> //')
    report $(echo "$REPL_OUT" | grep -qx "3" && echo 0 || echo 1) \
        "repl evaluates expressions ($(echo "$REPL_OUT" | grep -m1 . | head -c 40))"
    report $(echo "$REPL_OUT" | grep -qx "harness_repl_v" && echo 0 || echo 1) \
        "repl host string args/returns (settings round-trip)"
    # restore: drop the harness key from the settings file
    python3 - <<'CLEANKEY' 2>/dev/null
import json, os
p = os.path.expanduser("~/.config/havel-wm/settings.json")
try:
    d = json.load(open(p)); d.pop("harness_repl_k", None)
    json.dump(d, open(p, "w"), indent=2)
except Exception:
    pass
CLEANKEY
fi

# Output and keybinding introspection are plain IPC commands and remain
# available in builds without the Havel language embed.
GOUT=$(python3 - "$SOCK" <<'GOUT' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"get_outputs","params":{}})+"\n").encode())
print(s.recv(65536).decode())
GOUT
)
report $(echo "$GOUT" | grep -q '"count": *1' && echo "$GOUT" | grep -q '"width": *1280' && echo 0 || echo 1) \
    "get_outputs reports geometry ($(echo "$GOUT" | head -c 70))"
GKB=$(python3 - "$SOCK" <<'GKB' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"get_keybindings","params":{}})+"\n").encode())
print(s.recv(65536).decode())
GKB
)
GKB_N=$(echo "$GKB" | python3 -c "import sys,json; print(json.load(sys.stdin).get('result',{}).get('count',0))" 2>/dev/null)
report $([ "${GKB_N:-0}" -gt 5 ] && echo 0 || echo 1) \
    "get_keybindings lists bindings (count=${GKB_N:-err})"

# ---------------------------------------------------------------- phase 16: declarative config
# config.hv statements applied live: workspace_count (with window
# rehoming on shrink), gaps (pixel-verified window edge), floating_rule
# (spawn floats), default_layout (plugin routed), border_size.
if [ "${HAS_EMBED:-0}" -eq 0 ]; then
    skip "declarative config (no havel embed)"
else
    hrun() {
        python3 - "$SOCK" "$1" <<'HRUN' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"havel_run",
                       "params":{"source":sys.argv[2]}})+"\n").encode())
r = json.loads(s.recv(65536)); s.close()
print("ok" if "error" not in r else json.dumps(r["error"]))
HRUN
    }
    winfield() {
        python3 - "$SOCK" "$1" "${2:-}" <<'WFIELD' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"get_windows","params":{}})+"\n").encode())
r = json.loads(s.recv(65536)); s.close()
wins = r.get("result", [])
if isinstance(wins, dict): wins = wins.get("windows", [])
for w in wins:
    if "foot" in str(w.get("appId", "")) + str(w.get("title", "")):
        v = w.get(sys.argv[2], "")
        print(v if isinstance(v, str) else ("True" if v is True else ("False" if v is False else v)))
        break
WFIELD
    }
    # tile/unfloat the first foot window (placement is cascade by default)
    setfloat() {
        python3 - "$SOCK" "$1" <<'SFLT' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"get_windows","params":{}})+"\n").encode())
r = json.loads(s.recv(65536)); s.close()
wins = r.get("result", [])
if isinstance(wins, dict): wins = wins.get("windows", [])
for w in wins:
    if "foot" in str(w.get("appId", "")) + str(w.get("title", "")):
        s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"set_floating",
                               "params":{"id": w["id"], "floating": sys.argv[2] == "true"}})+"\n").encode())
        s.recv(65536); break
SFLT
    }

    # -- workspace_count: 10 -> 5 -> 3 with a window parked on ws 4
    hrun 'workspace_count(5)' >/dev/null
    sleep 0.3
    WSC=$(python3 - "$SOCK" <<'WSC' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"get_workspaces","params":{}})+"\n").encode())
r = json.loads(s.recv(65536)); s.close()
res = r.get("result", {})
if isinstance(res, dict): res = res.get("workspaces", [])
print(len(res) if isinstance(res, list) else -1)
WSC
)
    report $([ "$WSC" = "5" ] && echo 0 || echo 1) "workspace_count(5) applied (count=$WSC)"

    # winfield_ws targets ONLY the phase-16 foot (appId harness_ws):
    # earlier phases leave feet alive and winfield's generic "foot"
    # match returns a stale window (observed: move hit window 4
    # instead of 8, three phase-16 checks failed).
    winfield_ws() {
        # Retry up to 60 x 0.5s (30s): the foot's appId arrives
        # asynchronously via the set_app_id listener AFTER createXdgView -
        # a single immediate query raced it and saw an empty appId
        # (observed). 10 x 0.5s was still too short: at load ~27 the appId
        # took ~6s, so the lookup returned empty, the move call's
        # int("") threw (silenced), nothing moved, and the follow-up
        # read reported workspace 0 (observed).
        for _ in $(seq 1 60); do
            R=$(python3 - "$SOCK" "$1" <<'WFW' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(5)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"get_windows","params":{}})+"\n").encode())
r = json.loads(s.recv(65536)); s.close()
wins = r.get("result", [])
if isinstance(wins, dict): wins = wins.get("windows", [])
for w in wins:
    if w.get("appId", "") == "harness_ws":
        v = w.get(sys.argv[2], "")
        print(v if isinstance(v, str) else ("True" if v is True else ("False" if v is False else v)))
        break
WFW
)
            [ -n "$R" ] && { echo "$R"; return 0; }
            sleep 0.5
        done
        return 1
    }

    # spawn a window, move it to workspace 4 (index), then shrink to 3:
    # it must rehome to the last surviving workspace (index 2).
    # Own app-id: winfield matches the FIRST window containing "foot" and
    # older feet from earlier phases are still alive - a generic match
    # returned a stale id and the move hit the wrong window (observed).
    # Lifetime covers the whole phase: the opacity checks at the end of it
    # still read this window, and on a loaded box the steps in between
    # (gaps, floating_rule, default_layout, border_size) take well over the
    # 30s this window used to live - it died first and those reads came
    # back empty (observed: "opacity reads 1.0 before change (got )").
    (WAYLAND_DISPLAY=wayland-0 timeout 240 foot -a harness_ws -e sh -c 'sleep 210' > /dev/null 2>&1 &)
    sleep 1.5
    FOOT_ID=$(winfield_ws id)
    # Report the move's own reply: swallowing it turned "lookup returned
    # no id" and "the compositor rejected the move" into the same silent
    # no-op, and the check below then just read placeholder values back.
    MVR=$(python3 - "$SOCK" "${FOOT_ID:-}" <<'MVS' 2>&1
import socket, json, sys
if not sys.argv[2].strip():
    print("no window id from winfield_ws (appId not seen in time)"); sys.exit(0)
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"move_to_workspace",
                       "params":{"window_id": int(sys.argv[2]), "workspace": 4}})+"\n").encode())
print(s.recv(65536).decode()[:120])
MVS
)
    echo "    move_to_workspace -> ${MVR}"
    sleep 0.5
    WS4=$(winfield_ws workspace)
    report $([ "$WS4" = "4" ] && echo 0 || echo 1) "window on workspace 4 (ws=$WS4)"
    hrun 'workspace_count(3)' >/dev/null
    sleep 0.5
    WS_AFTER=$(winfield_ws workspace)
    report $([ "$WS_AFTER" = "2" ] && echo 0 || echo 1) \
        "shrinking rehomes windows (ws=$WS_AFTER, want 2)"
    hrun 'workspace(0)' >/dev/null
    hrun 'workspace_count(10)' >/dev/null

    # -- gaps: a fresh foot on the active workspace, tiled, edge tracked.
    # Own app-id: an earlier phase's foot would match a generic search and
    # the arrange would land on the wrong workspace.
    (WAYLAND_DISPLAY=wayland-0 timeout 60 foot -a harness_gaps -e sh -c 'sleep 55' > /dev/null 2>&1 &)
    GAP60CAP="$(mktemp /tmp/havel-gap60-XXXXXX.png)"
    GAP10CAP="$(mktemp /tmp/havel-gap10-XXXXXX.png)"
    sleep 2
    setfloat_harness() {
        python3 - "$SOCK" "$1" <<'SFLG' 2>/dev/null
import socket, json, sys
# ONE connection for the query and the mutation: fresh connections reuse
# fd numbers and a stale event-loop source race resets the second one.
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
def rpc(m, p):
    s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":m,"params":p})+"\n").encode())
    return json.loads(s.recv(65536))
r = rpc("get_windows", {})
wins = r.get("result", [])
if isinstance(wins, dict): wins = wins.get("windows", [])
for w in wins:
    if w.get("appId") == "harness_gaps":
        rpc("set_floating", {"id": w["id"], "floating": sys.argv[2] == "true"})
        break
s.close()
SFLG
    }
    setfloat_harness false
    hrun 'gaps(60)' >/dev/null
    sleep 1
    WAYLAND_DISPLAY=wayland-0 grim "$GAP60CAP" 2>/dev/null
    hrun 'gaps(10)' >/dev/null
    sleep 1
    WAYLAND_DISPLAY=wayland-0 grim "$GAP10CAP" 2>/dev/null
    GAPX=$(python3 - "$GAP60CAP" "$GAP10CAP" <<'GAPX' 2>/dev/null
from PIL import Image
import sys
def edge(p):
    im = Image.open(p).convert('RGB'); px = im.load()
    for x in range(0, 400):
        c = px[x, 400]
        if abs(c[0]-25) > 4 or abs(c[1]-25) > 4 or abs(c[2]-38) > 4:
            return x
    return -1
print(edge(sys.argv[1]), edge(sys.argv[2]))
GAPX
)
    G60=$(echo "$GAPX" | awk '{print $1}'); G10=$(echo "$GAPX" | awk '{print $2}')
    report $([ "${G60:-0}" -ge 50 ] && [ "${G60:-0}" -le 80 ] && [ "${G10:-0}" -le 30 ] && echo 0 || echo 1) \
        "gaps move the tiled edge (g60=${G60:-err} g10=${G10:-err})"
    rm -f "$GAP60CAP" "$GAP10CAP"

    # -- floating_rule: a foot with the ruled app-id must spawn floating
    hrun 'floating_rule("harness_float")' >/dev/null
    (WAYLAND_DISPLAY=wayland-0 timeout 30 foot -a harness_float -e sh -c 'sleep 20' > /dev/null 2>&1 &)
    sleep 1.5
    FLT=$(winfield floating)
    report $([ "$FLT" = "True" ] && echo 0 || echo 1) "floating_rule floats the match (floating=$FLT)"

    # -- default_layout + border_size reach their handlers
    hrun 'default_layout("grid")' >/dev/null
    report $(wait_for_log "Layout: grid" 5 && echo 0 || echo 1) "default_layout routed to the plugin"
    hrun 'border_size(4)' >/dev/null
    report $(wait_for_log "border_size -> 4" 5 && echo 0 || echo 1) "border_size applied"
    hrun 'border_size(1)' >/dev/null

    # -- opacity: IPC set_window_opacity reaches the screen (the model
    #    field alone never did - the C scene-buffer alpha was unwired).
    WSID=$(winfield_ws id)
    OP1=$(python3 - "$SOCK" "$WSID" <<'OPQ1' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"get_windows","params":{}})+"\n").encode())
r = json.loads(s.recv(65536)); s.close()
wins = r.get("result", [])
if isinstance(wins, dict): wins = wins.get("windows", [])
for w in wins:
    if w.get("id") == int(sys.argv[2]):
        print(w.get("opacity", "none")); break
OPQ1
)
    python3 - "$SOCK" "$WSID" <<'OPSET' >/dev/null 2>&1
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"set_window_opacity",
                       "params":{"id": int(sys.argv[2]), "opacity": 0.5}})+"\n").encode())
s.recv(65536); s.close()
OPSET
    sleep 0.5
    OP2=$(python3 - "$SOCK" "$WSID" <<'OPQ2' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"get_windows","params":{}})+"\n").encode())
r = json.loads(s.recv(65536)); s.close()
wins = r.get("result", [])
if isinstance(wins, dict): wins = wins.get("windows", [])
for w in wins:
    if w.get("id") == int(sys.argv[2]):
        print(w.get("opacity", "none")); break
OPQ2
)
    OP1OK=$(awk -v v="${OP1:-none}" 'BEGIN{print (v+0 == 1.0) ? 0 : 1}')
    report $([ "${OP1OK:-1}" = "0" ] && echo 0 || echo 1) "opacity reads 1.0 before change (got $OP1)"
    OP2OK=$(awk -v v="${OP2:-none}" 'BEGIN{print (v+0 == 0.5) ? 0 : 1}')
    report $([ "${OP2OK:-1}" = "0" ] && echo 0 || echo 1) "set_window_opacity applied (got $OP2)"
    # restore
    python3 - "$SOCK" "$WSID" <<'OPRST' >/dev/null 2>&1
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"set_window_opacity",
                       "params":{"id": int(sys.argv[2]), "opacity": 1.0}})+"\n").encode())
s.recv(65536); s.close()
OPRST
fi


# ---------------------------------------------------------------- phase 17: xwayland
# Closes checklist gap 6.6: an actual X client exercising the xwayland
# surface lifecycle (new surface -> associate -> C++ view -> get_windows)
# and the WM_DELETE close path. xred.c maps a solid-red window and
# loops on XNextEvent, so it only exits on a WM protocol error.
XRED="$BUILD_DIR/tests/xred"
if pgrep -x Xwayland >/dev/null 2>&1; then
    if [ ! -x "$XRED" ]; then
        mkdir -p "$BUILD_DIR/tests"
        cc -o "$XRED" tests/runtime/xred.c -lX11 || true
    fi
    # Read the display the compositor actually allocated. Assuming :0 is
    # wrong as soon as another X server holds that socket (this box
    # carries X0/X1/X2 from other sessions, so the harness Xwayland came
    # up on :1) - the probe then dialled the wrong server and found no
    # window. Same rule as the wayland socket elsewhere in this harness:
    # read the allocated name from the log, never assume it.
    XW_DISPLAY=$(grep -o "Starting Xwayland on :[0-9]*" "$LOG" | head -1 | grep -o ":[0-9]*$")
    if [ -z "$XW_DISPLAY" ]; then
        skip "xwayland phase (compositor announced no display)"
    elif [ -x "$XRED" ]; then
        (DISPLAY="$XW_DISPLAY" "$XRED" > /dev/null 2>&1 &)
        # Bounded wait for the X client to map: 2s was a race under load.
        for _ in $(seq 1 50); do
            grep -qa "xred-xwayland-test" "$LOG" && break
            sleep 0.2
        done
        # One probe returns both the match count and the window id.
        XPROBE=$(python3 - "$SOCK" <<'XW_PY' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"get_windows","params":{}})+"\n").encode())
r = json.loads(s.recv(65536)); s.close()
wins = r.get("result", [])
if isinstance(wins, dict): wins = wins.get("windows", [])
match = [w for w in wins
         if "xred-xwayland-test" in str(w.get("title","")) + str(w.get("app_id",""))]
print("%d %s" % (len(match), match[0].get("id", match[0].get("window_id", "")) if match else ""))
XW_PY
)
        XWINS=$(echo "$XPROBE" | awk '{print $1}')
        XRED_ID=$(echo "$XPROBE" | awk '{print $2}')
        report $([ "${XWINS:-0}" -ge 1 ] && echo 0 || echo 1) \
            "xwayland client mapped (get_windows xred count=${XWINS:-err})"

        if [ -n "$XRED_ID" ] && [ "$XRED_ID" != "" ]; then
            # WM_DELETE via IPC close: xred only exits on the WM protocol.
            python3 - "$SOCK" "$XRED_ID" <<'XCLOSE_PY' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"close",
                       "params":{"id": int(sys.argv[2])}})+"\n").encode())
s.recv(65536); s.close()
XCLOSE_PY
            sleep 2
            XRED_ALIVE=$(pgrep -x xred | wc -l)
            report $([ "$XRED_ALIVE" -eq 0 ] && echo 0 || echo 1) \
                "xwayland close terminates client (alive=${XRED_ALIVE})"
            report $(wait_for_log "\[XWayland\] DESTROY" 10 && echo 0 || echo 1) \
                "xwayland destroy path fired"
        else
            skip "xwayland close (window id not found)"
        fi
        pkill -x xred 2>/dev/null
    else
        skip "xwayland phase (xred build failed - libX11 dev missing?)"
    fi
else
    skip "xwayland phase (no Xwayland server)"
fi
# ---------------------------------------------------------------- phase 18: async PAM auth
# Tests the async PAM authentication path. Drives keys via havel_run +
# io.injectKey (the only IPC-exposed injection), checks lock state via
# is_screen_locked, and verifies the compositor keeps rendering through
# a wrong-password PAM delay (frames tick while the worker runs).
if pgrep -x Xwayland >/dev/null 2>&1; then
    # hrun sends one io.injectKey call: keycode + pressed
    keyrun() {
        python3 - "$SOCK" "$1" "$2" <<'KEYRUN' >/dev/null 2>&1
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
code = "io.injectKey(%s, %s)" % (sys.argv[2], sys.argv[3])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"havel_run",
                       "params":{"source":code}})+"\n").encode())
s.recv(65536); s.close()
KEYRUN
    }
    keytap() { keyrun "$1" true; keyrun "$1" false; }

    locked() {
        python3 - "$SOCK" <<'LCK' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"is_screen_locked","params":{}})+"\n").encode())
r = json.loads(s.recv(65536)); s.close()
print("yes" if r.get("result",{}).get("locked") else "no")
LCK
    }

    rpc_lock() {
        python3 - "$SOCK" <<'LSCR' >/dev/null 2>&1
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"lock_screen","params":{}})+"\n").encode())
s.recv(65536); s.close()
LSCR
    }

    rpc_lock
    sleep 0.5
    report $([ "$(locked)" = "yes" ] && echo 0 || echo 1) "lock_screen engages"

    # IPC liveness THROUGH the wrong-password PAM delay: the async worker
    # must keep the event loop serving requests (the old sync path froze
    # the whole compositor here). A fast reply proves the loop is free.
    ipc_rt() {
        python3 - "$SOCK" <<'IDC' 2>/dev/null
import socket, json, sys, time
s = socket.socket(socket.AF_UNIX); s.settimeout(15)
s.connect(sys.argv[1])
t0 = time.time()
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"is_screen_locked","params":{}})+"\n").encode())
r = json.loads(s.recv(65536)); s.close()
print("%.2f" % (time.time() - t0))
IDC
    }

    # Wrong password: '1' (keycode 10 evdev = digit 1) then Enter (28)
    keytap 10
    keytap 28
    sleep 0.3
    RT=$(ipc_rt)
    report $([ "$(locked)" = "yes" ] && echo 0 || echo 1) \
        "wrong password stays locked (async PAM verdict)"
    RTOK=$(awk -v r="${RT:-99}" 'BEGIN{print (r < 1.0) ? 0 : 1}')
    report $([ "${RTOK:-1}" = "0" ] && echo 0 || echo 1) \
        "IPC responsive during PAM delay (rt=${RT:-err}s)"

    # The real user password is unknowable here (and must never be
    # guessed), so "correct password unlocks" cannot be tested with
    # injected credentials. What CAN be verified: the unlock path works
    # from the locked state (documented unlock_screen IPC), and the
    # failed worker did not corrupt the lock.
    python3 - "$SOCK" <<'UNL' >/dev/null 2>&1
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"unlock_screen","params":{}})+"\n").encode())
s.recv(65536); s.close()
UNL
    sleep 0.5
    report $([ "$(locked)" = "no" ] && echo 0 || echo 1) \
        "unlock_screen releases the async-auth lock"
else
    skip "async PAM test (no Xwayland server)"
fi

# ---------------------------------------------------------------- phase 19: portal screenshot
# The xdg-desktop-portal backend (havel-portal) captures through
# ext-image-copy-capture-v1. It runs as its own process - the compositor
# must not own the portal DBus name - so this phase starts it against the
# harness compositor, asks the compositor's IPC for a portal screenshot,
# and checks the capture landed: the command is asynchronous (the capture
# needs the compositor's event loop free), so the outcome is polled from
# the result file it names.
echo "--- phase 19: portal screenshot"
PORTAL_BIN="$BUILD_DIR/bin/havel-portal"
if [ ! -x "$PORTAL_BIN" ]; then
    skip "portal backend not built (cmake --build build --target havel-portal)"
elif [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
    skip "portal phase (no session bus)"
else
    PORTAL_RESULT="${XDG_RUNTIME_DIR:-/tmp}/havel-portal-screenshot.json"
    rm -f "$PORTAL_RESULT"
    WAYLAND_DISPLAY=wayland-0 timeout 300 "$PORTAL_BIN" > /tmp/havel-portal-harness.log 2>&1 &
    PORTAL_PID=$!
    # Wait for the backend to own its bus name before asking.
    PORTAL_UP=1
    for _ in $(seq 1 60); do
        if dbus-send --session --dest=org.freedesktop.DBus --type=method_call \
             --print-reply /org/freedesktop/DBus org.freedesktop.DBus.GetNameOwner \
             string:org.freedesktop.impl.portal.desktop.havel >/dev/null 2>&1; then
            PORTAL_UP=0; break
        fi
        sleep 0.5
    done
    report "$PORTAL_UP" "portal backend owns its bus name"

    if [ "$PORTAL_UP" -eq 0 ]; then
        ACK=$(python3 - "$SOCK" <<'PACK' 2>/dev/null
import socket, json, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(30)
s.connect(sys.argv[1])
s.sendall((json.dumps({"jsonrpc":"2.0","id":1,"method":"portal_screenshot","params":{}})+"\n").encode())
print(s.recv(65536).decode())
PACK
)
        report $(echo "$ACK" | grep -q '"accepted": *true' && echo 0 || echo 1) \
            "portal_screenshot accepted (async)"

        PRES=1
        for _ in $(seq 1 60); do   # up to 60s: the capture waits for a frame
            if [ -s "$PORTAL_RESULT" ]; then PRES=0; break; fi
            sleep 1
        done
        report "$PRES" "portal capture reported a result"
        if [ "$PRES" -eq 0 ]; then
            URI=$(python3 -c "
import json,sys
try:
    d = json.load(open('$PORTAL_RESULT'))
    print(d.get('uri','') if d.get('success') else '')
except Exception:
    print('')" 2>/dev/null)
            report $([ -n "$URI" ] && echo 0 || echo 1) "portal capture succeeded ($(head -c 60 "$PORTAL_RESULT" 2>/dev/null))"
            PPIX=$(python3 - "$URI" <<'PCHK' 2>/dev/null || echo 0
import sys, urllib.parse
from PIL import Image
p = urllib.parse.unquote(urllib.parse.urlparse(sys.argv[1]).path)
im = Image.open(p).convert('RGB'); px = im.load(); w, h = im.size
# A real desktop capture, not an empty/black PNG: count non-black pixels.
print(sum(1 for y in range(0, h, 8) for x in range(0, w, 8) if px[x, y] != (0, 0, 0)))
PCHK
)
            report $([ "${PPIX:-0}" -gt 100 ] && echo 0 || echo 1) \
                "portal capture has content (non-black px: ${PPIX:-err})"
        else
            skip "portal capture content check (no result)"
        fi
    else
        skip "portal capture (backend never claimed its name)"
    fi
    kill "$PORTAL_PID" 2>/dev/null
    [ -n "${PORTAL_PID:-}" ] && wait "$PORTAL_PID" 2>/dev/null
fi

# ---------------------------------------------------------------- teardown
kill "$FOOT_PID" 2>/dev/null; FOOT_PID=""
report $([ "$(crashes)" -eq 0 ] && echo 0 || echo 1) "zero crashes across all phases"
if [ -n "$TERM_PROG" ]; then
    report $(wait_for_log "View freed" 10 && echo 0 || echo 1) "client teardown clean (View freed)"
else
    skip "client teardown (no terminal client was available)"
fi

kill "$WM_PID" 2>/dev/null; WM_PID=""
echo ""
echo "=== runtime verification: $PASS passed, $FAIL failed, $SKIPPED skipped ==="
if [ "$SKIPPED" -gt 0 ]; then
    echo "note: $SKIPPED check(s) did not run - see the SKIP lines above"
fi
[ "$FAIL" -eq 0 ] || { echo "log preserved: $LOG"; exit 1; }
rm -f "$LOG"
exit 0
