# tests/runtime — Headless Runtime Verification Harness

End-to-end verification of the compositor without a display. This harness
was the first kernel of execution-validating tests the project had; during
the 2026-09 audit it flushed out five load-bearing bugs that no static
pass could find (all fixed, see commit log `69d23d1..6822582`).

## What it does

Runs the compositor under `WLR_BACKENDS=headless WLR_RENDERER=pixman` with
`WLR_HEADLESS_OUTPUTS=1`, then drives it with real clients and synthetic
input:

1. **boot** — clean startup, loading-screen auto-hide timer fires, frame
   loop runs, no keybinding registration conflicts
2. **client** — terminal round-trip (connect → map → focus → render →
   unmap → destroy). Terminal-agnostic: foot (native #242424 background),
   kitty (`-o background=#242424`), or alacritty (config override) —
   first present wins
3. **pixels** — grim (wlr-screencopy-v1) captures a frame; the terminal's
   #242424 background is probed in mass to prove window content made it
   to the frame (capture retried up to 10s — first presented frame lags
   the map on slow boots)
4. **ipc** — 28 valid commands + 6 hostile inputs + 4 `havel_run` cases
   over the JSON-RPC socket (compositor must answer every one and survive)
5. **keys** — `zwp_virtual_keyboard_v1` injection: Meta+Return must
   trigger the spawn_terminal keybinding, Meta+2 the workspace_2 binding
6. **language-syntax hotkeys** (second compositor instance, its own
   `HAVEL_IPC_SOCKET`, config.hv loaded by the embed):
   - `^+F2 => {}` sugar and `keybind("Meta+F3", fn)` both register and
     fire through the same dispatch (external injection)
   - script-driven injection: `havel_run` snippets call
     `io.injectKey` through the persistent self-keyboard; the
     keybinding closure must fire **exactly once** (fired=1, trig=1 —
     the chunk-pin regression signature was 139 triggers / 0 closure
     logs, so presence-only checks are not enough)

18 checks total; exit 0 = all pass. On failure the compositor log is
preserved at the path printed in the output.

## Files

- `run_runtime_tests.sh` — orchestration; runs each phase, prints PASS/FAIL
- `ipc_probe.py` — JSON-RPC probe client (valid + hostile cases)
- `inject_keys.c` — tiny zwp_virtual_keyboard_v1 injector (wayland-client)
- `virtual-keyboard-unstable-v1.xml` — protocol XML for wayland-scanner

## Usage

```bash
./build.sh 6 build          # build the compositor first
bash tests/runtime/run_runtime_tests.sh
```

Build with the Havel language embedded (`libhavel_lang.a` from the sibling
monorepo, or `-DHAVEL_ARCHIVE_DIR=<dir>` pointing at a stable build tree)
for the full 18 checks; without it, phase 6 is skipped.

## Requirements

`foot`, `kitty` or `alacritty` (one of the three), `grim`, `python3`
(+ Pillow), `wayland-scanner`, `wayland-client`. No display, no GPU —
headless + pixman only.
