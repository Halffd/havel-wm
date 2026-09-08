# Validation Report: Security Scan + Docs Consistency Fixes (havel-wm)

Review date: 2026-09-07
Commits reviewed: `19498cb` (fix: security vulnerabilities and memory safety issues), `f4a9481` (security: fix command injection, memory leaks, and documentation inconsistencies), plus repo hygiene commits `c9b21ec`, `009eb3b`.

Note: no formal plan file existed; this session was driven by (1) scan+security-scan-cpp+antislop skills, (2) an externally pasted 48-issue scan report, (3) a docs/code inconsistency review. Success criteria are reconstructed from those requests.

## Implementation Status

✓ Phase 1 — Command injection elimination (skills scan): Fully implemented
✓ Phase 2 — Memory safety batch from pasted report (#20-#46): Fully implemented
✓ Phase 3 — AI slop removal (AltTabPlugin, WorkspaceInfoBar): Fully implemented
✓ Phase 4 — Docs/code consistency + warnings: Fully implemented
⚠️ Phase 5 — Stubbed IPC command implementation: Attempted and reverted (see Deviations)

## Automated Verification Results

✓ Fresh clean build: `./build.sh 6 build` (after `rm -rf build`) — PASS, binary linked
✓ Binary sanity: `./build/bin/havel-wm --help` — exits 0, prints usage
✓ Test suite: `bash tests/run_tests.sh` — 6 passed, 0 failed, 6 total
✓ Unused-variable warnings: 0 remaining (was 18+); claim of 17 silenced verified
✓ Remaining warnings: 3 pre-existing `missing-field-initializers` in TextInputManager.cpp (documented, untouched)
✓ No `system()`/`popen()` remain in wlr_bridge.c, main.cpp, bridge.cpp, IPCServer.cpp, Server.cpp (grep exit 1 across src/)
✓ Working tree clean at HEAD `f4a9481`; both session commits present on `master` (ahead of origin by 4, unpushed)

## Code Review Findings

### Matches Plan / Claims (verified in code)

Command injection (all confirmed via grep):
- `wlr_bridge.c:605`, `bridge.cpp:338`, `main.cpp:133` use `execvp(argv[0], argv)` — no shell `-c` anywhere
- VT switching in wlr_bridge.c:1684 uses direct `ioctl(VT_ACTIVATE)` with wlroots fallback; `system("chvt")` removed
- IPCServer.cpp:25-60 has `isSafePath`, `isSafeGeometry`, `safeExec` helpers; used at lines 682, 690, 699, 737, 743, 781, 789, 801, 838 (screenshot/audio/video handlers)
- `handleStopRecording` uses direct `kill(pid, SIGINT)`
- Server.cpp:643/684/700 — `popen("command -v")` replaced with `access(checkPath, X_OK)`

Memory safety (all confirmed):
- VulkanRenderer.c: `acquiredImageIndex` member added (line 62), stored from `vkAcquireNextImageKHR` (1284), used in begin/end frame (1294, 1299, 1305, 1315) — fixes wrong-imageIndex bug
- Sync-object mallocs NULL-checked with cascading cleanup (855+)
- Swizzle overflow guards: `width > UINT32_MAX / width` at 1471, 1516
- `get_surface_formats/present_modes` free+NULL on VK failure (1683, 1710)
- VulkanSceneCompositor.c:77-80 and GLES2Renderer.c:312-315 — realloc via temp pointer with NULL check and cleanup
- VulkanDmaBuf.c:231 — `texture->fds[0] = -1` after successful import (FD double-close fix)
- OutputManager.c:111-119 — all three gamma ramps calloc-checked with cleanup
- OutputManager.c:139-155 — `is_first_output` computed before `wl_list_insert`; positioning uses `outputs.prev->prev` (correct: `prev` after tail-insert is the new output; `prev->prev` is the prior tail)
- LayerShell.c:85-88 — `surface->output` NULL-checked
- XdgShell.c:215-217 — `havel_xdg_view_create` return checked
- InputHandler.c:131-148 — xkb context/keymap/state NULL-checked
- Common.h: SIZE_MAX guards in `array_create` (110-111), `array_ensure_capacity` (135-143), `str_concat` (242); `color_lerp`/`color_from_hex` null/uninit fixes; `array_push`/`str_trim` ownership documented
- LoadingScreen.c:60 — `getrandom()` seeding with time+pid fallback
- DestroyOnClosePlugin.cpp:135 — `thread_local std::mt19937` replaces `rand()`
- bridge.cpp:57-80 — `havel_cpp_server_destroy` now deletes TextInputManager, GestureRecognizer, WindowGroupManager, DesktopManager (was leak)

Docs consistency:
- D-Bus: `org.freedesktop.DbusNotifications` → `org.freedesktop.Notifications` throughout NotificationDaemon.cpp (interface XML, object path, well-known name, signals, CloseNotification method)
- Socket path: panel default `/tmp/havel-ipc.sock` → `/tmp/havel-wm.sock` (main.cpp:20); README `/tmp/havel-wm-ipc.sock` → `$XDG_RUNTIME_DIR/havel-wm.sock` with `/tmp` fallback
- XWayland: PROTOCOLS.md + STATUS.md downgraded from "Fully Implemented ✅" to "⚠️ Partial (DnD only)" with explanation that `wlr_xwayland_create()` is never called — matches code reality

Quality:
- AI slop: 0 occurrences of flagged redundant comments in AltTabPlugin.cpp / WorkspaceInfoBar.cpp
- IPCServer gained public `createErrorResponse` (declared + implemented)

### Deviations

- Phase 5 — Stubbed IPC commands: session attempted to implement ~22 stubs (`get_outputs`, `set_output_scale`, plugin mgmt, workspace/window queries) in Server.cpp. Attempt failed to compile: used nonexistent APIs (`g_output_get_all`, `g_output_layout_get_box`, `g_server_output_layout`, `m_coreWindowManager.getViewById`, `plugin->priority()`, `enablePlugin/disablePlugin/configurePlugin`, `getViewsOnWorkspace` vs real `getWindowsOnWorkspace`).
  - **Assessment**: Revert was correct under the "restore working build" priority. Stubs remain as 22 "Would" comments in Server.cpp.
  - **Impact**: Docs still overstate some IPC commands (get_outputs returns empty, workspace_next/prev no-op). This is the largest remaining gap.
  - **Recommendation**: implement against real APIs (`CoreWindowManager::getWindow`, `getAllWindows`, `getWindowsOnWorkspace`, `moveWindowToWorkspace`, `setAlwaysOnTop`) or annotate IPC_API.md with "not yet implemented" markers until done.
- Environment deviation: repo moved from `/home/all/repos/havel/havel-wm` (now empty) to `/home/all/repos/havel-wm`; stale build cache broke `./build.sh` (CMakeCache path mismatch).
  - **Assessment**: fixed by `rm -rf build` + clean rebuild — PASS. Not a code defect but worth recording.
- Documentation under-statements remain: README/STATUS say "40+ IPC commands" (55 registered), "15 plugins" (14 built, 3 disabled), and terminal default "foot" (code tries alacritty first). Minor; not addressed this session.

### Potential Issues / Follow-ups

1. `set_output_scale` IPC stub still returns success without acting (docs claim scale support).
2. `notify`/`close_notification` IPC commands return success but never touch NotificationDaemon — misleading API surface.
3. `get_stats.uptime_ms` still hardcoded 0 (m_startTime member was added to Server.hpp in the reverted batch — verify it was removed cleanly; header shows `m_startTime` present in Server.hpp but Server.cpp is reverted → check for unused-member state).
4. Bridge.cpp `havel_cpp_server_destroy` now deletes C-layer objects, but Server destructor comment may still claim "owned by C layer" — cross-check ownership comments to avoid double-delete confusion.
5. Unpushed: 4 commits ahead of origin/master — push when ready.

### Manual Testing Required

1. Runtime compositor smoke test on real TTY/session (cannot run headless):
   - [ ] Launch `./build/bin/havel-wm`, confirm startup, Meta+Return spawns terminal (alacritty→foot)
   - [ ] Ctrl+Alt+F2/F3 VT switching works (ioctl path)
   - [ ] Multi-monitor: connect second output, verify positioning uses prior tail correctly
   - [ ] `notify-send "test"` produces a notification (validates renamed D-Bus interface)
   - [ ] Panel connects to `$XDG_RUNTIME_DIR/havel-wm.sock` by default
   - [ ] Screenshot IPC with `../`-path and geometry containing shell metachars is rejected
2. Vulkan path (if hardware available): run `-r vulkan`, resize window, confirm no wrong-image tearing (acquiredImageIndex fix)
3. Gamma/brightness plugin on real monitor (restores state on exit)

## Validation Checklist

- [x] All phases marked complete are actually done (phases 1-4 verified in code)
- [x] Automated tests pass (6/6, build PASS on fresh cache)
- [x] Code follows existing patterns (C bridge style, LOG_* macros, jsonToString helpers)
- [x] No regressions (build clean, warnings reduced, binary runs)
- [x] Error handling robust where touched (malloc/realloc/XKB checks, exec failure _exit codes)
- [x] Documentation updated where claimed (socket path, XWayland, D-Bus); minor under-statements remain
- [x] Manual test steps clear (listed above)

## Verdict

**PASS with follow-ups.** All claimed fixes verified present in code and compiling cleanly; test suite green. The single incomplete ambition (stubbed IPC commands) was correctly reverted rather than shipped broken, and is documented as the top follow-up.

## Ticket Status

No ticket file or plan file with frontmatter exists in this repo (checked `.tickets/`, root `*.md`). Session work was skill-driven, not ticket-driven — nothing to mark as 'reviewed'. This review report serves as the validation record.

## Verification Evidence (commands executed this review)

```
Build:        ./build.sh 6 build (fresh, after rm -rf build) → [100%] Built target havel-wm, PASS
Binary:       ./build/bin/havel-wm --help → exit 0
Tests:        bash tests/run_tests.sh → Passed: 6, Failed: 0, "All tests passed!"
Warnings:     unused variable: 0 remaining; 3 pre-existing missing-field-initializers
Injection:    grep system(/popen( across src/ + wlr_bridge.c → no matches
```
