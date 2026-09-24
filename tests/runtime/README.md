# tests/runtime — Headless Runtime Verification Harness

End-to-end verification of the compositor without a display. This harness
was the first kernel of execution-validating tests the project had; during
the 2026-09 audit it flushed out five load-bearing bugs that no static
pass could find (all fixed, see commit log `69d23d1..6822582`).

## What it does

Runs the compositor under `WLR_BACKENDS=headless WLR_RENDERER=pixman` with
`WLR_HEADLESS_OUTPUTS=1`, then drives it with real clients and synthetic
input:

1. **boot** — clean startup, loading-screen auto-hide timer fires, frame loop runs
2. **client** — foot round-trip (connect → map → focus → render → unmap → destroy)
3. **pixels** — grim (wlr-screencopy-v1) captures a frame; the terminal's
   background tint is probed to prove window content made it to the frame
4. **ipc** — 28 valid commands + 6 hostile inputs over the JSON-RPC socket
   (compositor must answer every one and survive)
5. **keys** — `zwp_virtual_keyboard_v1` injection: Meta+Return must trigger
   the spawn_terminal keybinding in the log

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

Exit 0 = all phases pass. On failure the compositor log is preserved at
the path printed in the output.

## Requirements

`foot`, `grim`, `python3` (+ Pillow), `wayland-scanner`, `wayland-client`.
No display, no GPU — headless + pixman only.
