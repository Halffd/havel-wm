# XWayland Checklist

Complete inventory of XWayland support in havel-wm: every code path,
what it does, how to verify it, and its honest status as of
2026-10-26 (commit 90c20bf).



The compositor creates the XWayland server eagerly at boot and exports
`DISPLAY` (wlr_bridge.c:3366-3372). `Xwayland` binary is at
/usr/bin/Xwayland. `xprop` is available; xeyes/xclock/xterm/xmessage
are NOT installed — for client probes use any installed X client
(`xlsclients` also missing; use `xprop -root` or a toolkit app).

---

## 1. Server lifecycle

| # | Item | Code path | How to verify | Status |
|---|------|-----------|---------------|--------|
| 1.1 | Eager XWayland server created at compositor boot (lazy=false: the lazy variant accepted connections, created a surface, then died in a restart loop) | `wlr_bridge.c:3366` `wlr_xwayland_create(display, compositor, false)` in server init | Boot compositor; `pgrep Xwayland` shows the server | **VERIFIED 2026-10-26 headless: `Xwayland :0 -rootless -core -terminate ... -wm <pid>` up; log "Xserver is ready"** |
| 1.2 | `DISPLAY` exported and inherited by children | `wlr_bridge.c:3371` `setenv("DISPLAY", ...->display_name, 1)` | `DISPLAY=:0 xprop -root` connects | **VERIFIED 2026-10-26: setenv runs post-exec so /proc/PID/environ (exec-time snapshot) shows nothing — expected; xprop connecting proves propagation** |
| 1.3 | `new_surface` listener wired | `wlr_bridge.c:3368-3369` | Log line `"[XWayland] View setup complete for"` on first X client | covered by 2.2 smoke |
| 1.4 | XWayland survives client crash (server stays up) | eager mode keeps Xwayland alive across client death | kill an X client; `pgrep Xwayland` still alive | UNVERIFIED — needs an X client |
| 1.5 | Shutdown: Xwayland dies with compositor (wlroots destroys it with the display) | wlroots-owned; no manual teardown needed | kill compositor; `pgrep Xwayland` empty | **VERIFIED 2026-10-26 headless: after `pkill havel-wm`, `pgrep -c Xwayland` = 0** |

X WM handshake smoke (2026-10-26, headless): `DISPLAY=:0 xprop -root`
answers with the full EWMH set — `_NET_SUPPORTED` lists 19 atoms
(FULLSCREEN, MAXIMIZED_VERT/HORZ, HIDDEN, ABOVE, BELOW, STICKY, ...),
plus `_NET_ACTIVE_WINDOW` and `_NET_CLIENT_LIST` — so havel-wm registers
as a functioning X window manager. `_XKB_RULES_NAMES` reports
evdev/pc105/us.

## 2. Surface lifecycle (map/unmap/reassociate)

| # | Item | Code path | How to verify | Status |
|---|------|-----------|---------------|--------|
| 2.1 | `override_redirect` windows ignored (no view, no scene node) | `wlr_bridge.c:1397` early return in `server_new_xwayland_surface` | spawn a menu/tooltip X client (override-redirect); no view appears in `get_windows` | UNVERIFIED — needs X client |
| 2.2 | View struct created, kind-tagged `HAVEL_VIEW_XWAYLAND` (0x58574C56), zero-initialized | `wlr_bridge.c:1403-1404` | log `[XWayland] View setup complete`; `get_windows` lists it | **VERIFIED 2026-10-27 headless (harness phase 17): xred maps, `get_windows xred count=1`** |
| 2.3 | Scene tree created immediately (wl_surface may not exist yet) | `wlr_bridge.c:1419-1424` | associate handler binds the scene surface when ready | **VERIFIED 2026-10-27: `surface associated: binding scene surface` in harness run** |
| 2.4 | **Associate ordering hazard**: wl_surface is NOT valid at new-surface time; scene surface created only on the `associate` event, guarded by `scene_surface_created` (double-create guard); dissociate resets the flag so re-associate rebinds | `wlr_bridge.c:429-437` (comment), `1336-1359` (associate), `1361-1369` (dissociate) | an X client that rebinds its surface; log shows `surface associated: binding scene surface` again | VERIFIED by design (comment records the SIGSEGV that motivated it); runtime rebind UNVERIFIED |
| 2.5 | No `events.map` for xwayland: C++ `onViewMapped` fires from associate (spawn offset, focus, thumbnail) | `wlr_bridge.c:1349-1357` | first frame shows the window; `[Server] onViewMapped` in log | **VERIFIED 2026-10-27 (harness: window visible in get_windows right after associate)** |
| 2.6 | Destroy: cpp_view cleared FIRST, listeners removed, C++ notified (it frees the View), scene-graph view freed C-side, struct freed exactly once | `wlr_bridge.c:1303-1333` | close an X client; log `DESTROY`; no ASAN report (debug builds run ASAN) | **VERIFIED 2026-10-27 headless (harness: `xwayland destroy path fired` PASS after WM_DELETE close)** |
| 2.7 | C++ View for xwayland surfaces — floating by default, active workspace, registered with window manager | routes through `createXdgView` with the X class as appId (`wlr_bridge.c` new_surface); the dedicated `createXwaylandView` was dead code and is deleted | `get_windows` shows the window, floating | **VERIFIED 2026-10-27 (harness: window listed and closable by id)** |

## 3. Positioning and stacking (cascade)

| # | Item | Code path | How to verify | Status |
|---|------|-----------|---------------|--------|
| 3.1 | Client-requested position honored at creation ONLY when the client asked for a real position (not 0,0, not the -1 sentinel) and it lands inside the layout | `wlr_bridge.c:1430-1446` | spawn a positioned X client; window at its coords | UNVERIFIED — needs X client |
| 3.2 | Fixed cascade spawn offset applied on map; wraps after the eighth diagonal (X and XDG share the offset logic) | `src/wm/core/Server.cpp:322` (comment), `havel_wlr_set_view_position` sets `pending_cascade_x/y` (wlr_bridge.c:4033-4038) | spawn several unpositioned X clients: successive diagonal offsets, not all at (0,0) | covered by 2.2 multi-spawn |
| 3.3 | Cascade override in the configure handler: a client that re-requests its own (0,0) position at map time gets the compositor's cascade re-applied — but the **client's requested size (event->width/height) now applies** (was: current size, which froze X11 client-initiated resizes) | `wlr_bridge.c:1371-1393` (fixed in 90c20bf) | xprop -f or an X client that resizes itself; size change lands | **regression-fixed this session, needs live re-verification on DRM** |
| 3.4 | `xwayland_view_set_position` forwards direct position sets through configure (keeps X and scene node in sync) | `wlr_bridge.c:826-832` | move window via IPC `move`; both compositor model and X server agree | covered by 2.2 + IPC |
| 3.5 | Meta+click drag works on X windows (interactive move reads scene node offsets — wrapper-layout independent) | `wlr_bridge.c:2843-2866` (generic grab path) | needs real pointer — DRM only | **cannot verify headless** |

## 4. Input and focus

| # | Item | Code path | How to verify | Status |
|---|------|-----------|---------------|--------|
| 4.1 | Click-to-focus on X surfaces: hit test falls through to the xwayland branch, `wlr_xwayland_surface_activate`, raise, keyboard enter | `wlr_bridge.c:2892-2900` | click an X window; it takes keyboard | needs real pointer / inject via warp_cursor+click_pointer IPC |
| 4.2 | Keyboard focus delivered to X via `wlr_xwayland_set_seat` — **FIXED this session**: the seat is now wired at the first backend input device and again at the first virtual-keyboard arrival (the path that fires under headless; the backend exposes zero input devices there), idempotent via `server->xwayland_seat_set` | boot log: `[XWayland] seat wired for X key events (virtual keyboard)` — observed in headless smoke | type into a focused X client on DRM — delivery to a real client still needs a live X app | **wired + smoke-verified headless; client-level delivery UNVERIFIED (no X clients installed)** |
| 4.3 | Screen lock gates input for X clients too (the C-side lock gates run before any seat notify) | `wlr_bridge.c:2780-2785` (button), `2903-2906` (axis), `2705-2711` (motion) — fixed in 90c20bf | lock screen; X client receives nothing | fixed this session; headless-verified logic, X-client runtime UNVERIFIED |
| 4.4 | `close_view` on an X window sends `wlr_xwayland_surface_close` (WM_DELETE) | `wlr_bridge.c:621-625` (kind-tagged dispatch) → `CoreWindowManager::closeWindow` m_onClose → `havel_wlr_close_view` (the callback was never registered before — IPC close only untracked the window; wired in Server's actions setup) | IPC `close` on an X window; client exits cleanly (not killed) | **VERIFIED 2026-10-27 headless (harness: `xwayland close terminates client (alive=0)` PASS)** |
| 4.5 | App-id matching / window rules see X windows' class as app id | `wlr_bridge.c:1448-1449` (`xsurface->class` passed as appId into `createXdgView`) | `floating_rule("some-x-class")` matches an X client | covered by 2.2 (`get_windows` app field) |

## 5. State operations (kind-tagged dispatch — all XDG/XWayland pairs)

Every C++ → C view operation dispatches on the first struct member
(`enum havel_view_kind` tag, wlr_bridge.c:378-381) so a native handle
is never miscast (the old unconditional cast crashed on XWayland).

| # | Operation | XWayland branch | Status |
|---|-----------|------------------|--------|
| 5.1 | set position (with cascade flag) | `wlr_bridge.c:471-480` | VERIFIED compiling; runtime covered by 2.2 |
| 5.2 | set size | `wlr_bridge.c:494-500` (`wlr_xwayland_surface_configure`) | covered by 2.2 |
| 5.3 | close | `wlr_bridge.c:621-625` | see 4.4 |
| 5.4 | fullscreen | `wlr_bridge.c:638-643` (`set_fullscreen`) | UNVERIFIED — needs X client |
| 5.5 | minimize (hide scene node) | `wlr_bridge.c:656-659` — scene_tree at same offset in both structs | UNVERIFIED |
| 5.6 | focus | `wlr_bridge.c:558-563` (kind-tagged scene-tree resolution) | see 4.1 |
| 5.7 | raise | `wlr_bridge.c:522-527` | see 4.1 |
| 5.8 | thumbnail capture | `wlr_bridge.c:594-600` (`capture_view_pixels` via scene tree) | UNVERIFIED — X window in Alt-Tab grid |
| 5.9 | workspace move / reparent | `wlr_bridge.c:4099-4120` `havel_wlr_reparent_view_workspace` (added 90c20bf) — kind-tagged, handles XWayland scene trees | **added this session; X-window runtime UNVERIFIED** |

## 6. Known gaps (documented, not hidden)

| # | Gap | Evidence |
|---|-----|----------|
| 6.1 | ~~`wlr_xwayland_set_seat` never called~~ **FIXED (this session)**: wired at first backend input device AND at first virtual-keyboard arrival (headless has zero backend devices, so the virtual-keyboard hook is the one that fires there). Idempotent via `server->xwayland_seat_set`. | wlr_bridge.c `server_new_input` + `server_new_virtual_keyboard`; boot log: "[XWayland] seat wired for X key events (virtual keyboard)" — **observed in headless smoke** |
| 6.2 | ~~Missing set_title/set_class/request_fullscreen/request_maximize/request_activate listeners~~ **FIXED (this session)**: all five added with XDG-equivalent handlers (title/class propagate to the C++ View + window model; fullscreen forwards through `wlr_xwayland_surface_set_fullscreen`; maximize configures to full output size; activate raises + activates). Removed again in the destroy path. | wlr_bridge.c `xwayland_handle_*` handlers + registration in `server_new_xwayland_surface` + `wl_list_remove` in `xwayland_view_handle_destroy` |
| 6.3 | XWayland DnD (`src/wm/core/XWaylandDnD.cpp`) is a WIP stub — **excluded from the build** (CMakeLists.txt:179-181) and `handleXWaylandDnDEvent` has no caller; Xdnd messages cannot reach it | CMakeLists.txt:179 comment "XWaylandDnD - WIP, needs wlroots integration" — still open |
| 6.4 | ~~`createXwaylandView` dead code~~ **FIXED (this session)**: deleted from Server.cpp and Server.hpp; a comment at the former declaration site records that XWayland surfaces route through `createXdgView` with the X class as appId. | git grep createXwaylandView → only the explanatory comment |
| 6.5 | Scene-graph xwayland views (`scene_view_create_xwayland`) hardcode float geometry 640x480 at (150,150) — cosmetic; the real layout comes from the wlr scene tree | wlr_bridge.c:3986-3989 |
| 6.6 | ~~No harness coverage for XWayland~~ **FIXED (this session)**: harness phase 17 compiles `xred.c` on the fly, spawns it under the compositor's xwayland, and asserts the full lifecycle — map (get_windows), WM_DELETE close (client exits), destroy log. All three PASS headless; the phase immediately caught a real bug (the unregistered close callback). | tests/runtime/run_runtime_tests.sh phase 17; harness now 74 passed / 0 failed / 0 skipped |

## 7. Environment for verification

- `Xwayland`: /usr/bin/Xwayland — present
- `xprop`: present (root-window probes only; `xprop -root WM_NAME` works without a client)
- X test clients (xeyes/xclock/xterm/xmessage/xlsclients): **absent** —
  install `xorg-xeyes`/`xorg-xclock`/`xterm` or use an installed toolkit
  app before running the runtime checks above
- Headless compositor can host X clients: Xwayland does not need DRM

## 8. Suggested verification order (one shell session)

```bash
# 1. boot + server up
pgrep -a Xwayland                                   # 1.1
tr '\0' '\n' < /proc/$(pgrep -x havel-wm)/environ | grep DISPLAY   # 1.2
# 2. spawn an X client (after installing one), then over IPC:
#    get_windows -> xwayland entry (2.2, 4.5)
#    move / resize -> window obeys (3.4, 5.2)
#    close -> clean exit (4.4)
# 3. keyboard: focus the X window, type (4.2 — watch for the seat gap)
# 4. teardown: kill client -> DESTROY log, no ASAN (2.6)
# 5. kill compositor -> pgrep Xwayland empty (1.5)
```

## Status summary

- VERIFIED live (headless harness/smoke): server lifecycle 1.1/1.2/1.5,
  surface lifecycle 2.2/2.3/2.5/2.6/2.7, title/class propagation (6.2
  listeners logged at runtime), seat wiring (6.1, boot log), close
  path 4.4 (after wiring the never-registered m_onClose), and harness
  coverage itself (6.6)
- Session fixes: 3.3 configure size, 4.3 lock gates, 5.9 reparent,
  6.1 seat, 6.2 five listeners, 6.4 dead code, 6.6 harness phase,
  plus the close-callback bug the new phase caught
- Still open: 6.3 (XWaylandDnD stub, excluded from build), 2.1/2.4
  runtime rebind (needs override-redirect + surface-rebinding X
  clients), 4.1/4.2 client-level input on DRM, 6.5 (cosmetic)
- Runtime harness: 74 passed, 0 failed, 0 skipped
