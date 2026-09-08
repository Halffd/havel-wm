# havel-wm refactoring plan

Evidence-based, ordered by dependency and risk. Every phase has a verification gate.
Estimated total: ~5,300 lines deleted, ~2,000 moved, 2 god files split, 1 merge.

Baseline (measured):
- 53,338 lines of source across src/ + wlr_bridge.c
- wlr_bridge.c: 2,983 lines, 87 functions, 116 listener refs
- Server.cpp: 1,836 lines; startIPCServer alone is 357 lines with 55 registerCommand calls
- Server.hpp: 125 public methods
- Common.h: 1,139 lines, 84 inline functions
- 25 stub IPC commands returning fake success ("Would ..." comments)
- Known dead: BackendCApi.c/h (compiled, zero includers), duplicate notification daemon, 4 orphan shell apps, repomix-output.xml + BLACK_SCREEN_ANALYSIS.md tracked

---

## Phase 0: delete dead weight (zero risk, pure win)

**Delete (evidence in parentheses):**

| Target | Lines | Evidence |
|---|---|---|
| `src/wm/backend/BackendCApi.c` + `.h` | 1,291 | in CMakeLists but zero files include the header |
| `src/shell/notifications/` (whole dir) | 175 | diverged duplicate of the built `wm/core/NotificationDaemon` (312 lines), not in CMake |
| `src/repomix-output.xml` | - | generated dump, tracked by mistake |
| `BLACK_SCREEN_ANALYSIS.md` | - | historical debugging note; superseded by git history |
| `src/wm/plugins/ExamplePlugin.cpp` | - | decide: keep as plugin-API living doc, or delete. Recommendation: keep, it compiles and documents the interface |

**Orphan shell apps — decision required:**
`dictionary/` (1,738 lines), `videoplayer/` (1,274), `gamelauncher/` (793), `updater/` (898) each have a local CMakeLists but are never `add_subdirectory`'d. Two defensible options:
- (a) wire them in — only after verifying each builds standalone; they are complete-looking apps
- (b) delete them — consistent with the scope-consolidation decision
Recommendation: (b) delete. Rationale: same class of unscoped parallel investment that stalled the project; the WM core is the product. If any is wanted later, git history preserves it. Total: ~4,700 lines.

Action: `git rm -r`, strip any CMake references, clean build.

**Gate:** `./build.sh 6 build` from clean cache, `tests/run_tests.sh` 6/6, binary runs.

---

## Phase 1: IPC honesty — implement or remove the 25 stubs

Current state: 25 commands registered in Server.cpp return fabricated success (`// Would ...`). Docs (IPC_API.md, 1,180 lines) promise them. Worst offenders: `workspace_next/prev` (no-op), `set_output_scale` (no-op), `get_outputs` (empty), `notify` (never touches the daemon), `get_plugins` (empty), `set_window_opacity/fullscreen/always_on_top` (no-op), `move_to_workspace` (no-op).

**Implement (real APIs verified to exist):**
- `get_window`, `get_windows_by_app` → `CoreWindowManager::getWindow(id)`, `getAllWindows()`, `getWindowsOnWorkspace(ws)`
- `set_window_fullscreen` → `Window::setFullscreen(bool)` (CoreWindowManager.cpp:264 pattern)
- `set_window_opacity` → `Window::setOpacity(float)` (pattern at CoreWindowManager.cpp:444)
- `set_window_always_on_top` → `CoreWindowManager::setAlwaysOnTop(window, onTop)`
- `move_to_workspace` → `CoreWindowManager::moveWindowToWorkspace(id, ws)` (returns void — drop the bool check)
- `workspace_next/prev`, `get_workspaces` → `Server::setActiveWorkspace`, `m_workspaces[]`, `WORKSPACE_COUNT`
- `get_display_settings` → read `m_gamma`/`m_temperature`/`m_brightness` members (exist in Server.hpp)
- `get_stats` → real uptime from `m_startTime` (member exists in Server.hpp)
- `notify`/`close_notification` → `DbusNotificationDaemon` (used by NotificationsPlugin already)
- `get_plugins`/`enable/disable` → check PluginManager surface; if `plugins()` accessor or equivalent exists use it, otherwise add a minimal one

**Remove or annotate (no honest implementation path):**
- `set_output_scale`, `set_zoom` → need C-bridge functions that do not exist; remove from registration and add a "not implemented" note in IPC_API.md
- `get_outputs` → needs wlr_output iteration exposed through the bridge; if trivial via existing g_server handles, implement; otherwise annotate

**Gate:** build; then manual IPC smoke: `echo '{"method":"get_workspaces"}' | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock` returns real data. Each implemented command exercised once.

---

## Phase 2: extract IPC command registration from Server.cpp

`startIPCServer()` is a 357-line function with 55 inline lambdas; 18 of them call `extractJson*` helpers that belong to IPCServer, not Server.

**Refactor:**
- Each command becomes a named `IPCServer::handleX(const std::string&)` method (pattern already exists: `handleScreenshot`, `handleAudioRecord`, ...)
- Server-side commands that need Server internals (window manager, plugin manager, workspaces) take a registration context: `ipc->registerCommand("get_window", [this](auto& a){ return handleGetWindow(a); })` or pass a `ServerIpcContext` struct of std::function callbacks — pick the lighter one: member methods on IPCServer + a small context of Server-provided callbacks for state access
- The `createErrorResponse` public helper added in `f4a9481` is the error path for all of these
- Target: Server.cpp shrinks by ~300 lines; IPCServer.cpp grows accordingly but each handler is individually greppable

**Gate:** build; command-count parity — registration table must still contain exactly 55 entries (minus any removed in Phase 1); IPC smoke on 3 representative commands.

---

## Phase 3: split the Server god object

Server.hpp: 125 public methods; the class mixes composition-root duties with workspace state, display settings, overlay state, keybinding dispatch, animation, grab-state.

**Extract (each keeps Server as composition root, holding unique_ptrs):**
- `WorkspaceManager` — **executed phase 3, verdict: do not extract.** The
  m_workspaces[] array has 15 direct touchpoints woven through view
  creation, grab handling, and arrangeWorkspace; it is the layout
  engine's working memory, not separable state. Extracting would either
  relocate Server's hot path (unverifiable without a live session) or
  create a hollow state-holder (indirection with no benefit). Access
  continues via m_workspaces[] + activeWorkspace()/setActiveWorkspace().
- `DisplaySettings` — **done.** Owns gamma/temperature/brightness/
  grayscale/negative; applies through C bridge handle injected by
  Server::setNativeHandle. Server setters are forwarders (GammaPlugin
  and C bridge see identical API).
- `OverlayController` — **done.** Owns alt-tab/overview/launcher state
  machines; data dependencies injected as four callbacks
  (collectWindows, focusWindow, switchWorkspace, spawnApp) wired in
  the Server constructor. Server's 20 overlay methods are one-line
  forwarders.
- Grab-state machine (`GrabMode` enum, pointer-button flow) can stay in Server — it is genuinely core-event routing

**Do not extract:** keybinding dispatch (it fans out into everything — extraction would create a circular dependency).

**Gate:** build, tests, binary smoke. No public API change for plugins (CompositorAPI surface untouched).

---

## Phase 4: unify the two window managers

Two managers track the same windows independently:
- `shell/WindowManager` (457 lines, 4 users) — record store (WindowEntry: id/appId/title/workspace/flags) feeding IPC
- `wm/core/CoreWindowManager` (675 lines, 4 users) — real View/Window management, workspace movement, rules

Risk: state desync between them (the IPC layer can report windows the core layer has already destroyed or moved).

**Refactor direction: single source of truth.**
- CoreWindowManager becomes the only owner of window state
- shell/WindowManager becomes a thin projection: `getWindows()` maps `CoreWindowManager::getAllWindows()` → IPC JSON entries; `focusedWindow()` maps `getFocusedView()`; mutation calls delegate to core
- Delete the shell manager's own registration/tracking state (id assignment stays in core)
- Keep the class name and header path to avoid churning all 4 consumers; change internals only

**Gate:** build; manual IPC `get_windows` returns live data matching on-screen windows; window open/close during a `get_windows` poll shows no stale entries.

---

## Phase 5: split wlr_bridge.c

2,983 lines, 87 functions, 116 listener references — the monolith. The module skeleton already exists in `src/wm/backend/{server,output,input,shell}/`.

**Split by wlroots concern, moving functions out of wlr_bridge.c into the existing backend modules:**
- `backend/server/` — display/backend/session/allocator/renderer/compositor init (the big `havel_wlr_create` at :2148 region), main-loop wiring, `havel_wlr_destroy`
- `backend/output/` — OutputManager already owns output lifecycle; move the output listeners (frame, mode, destroy) currently in wlr_bridge.c
- `backend/input/` — keyboard/pointer/touch/VT-switch handlers (the Ctrl+Alt+F1..F12 ioctl path), seat init
- `backend/shell/` — xdg-shell and layer-shell listeners
- wlr_bridge.c keeps: the C-API boundary the C++ side includes (`wm/wlr_bridge.h`), plus the g_* function-pointer tables the C++ core sets — it remains the ABI seam, nothing else

Mechanically: one concern per commit, move listeners + their statics together, keep function signatures unchanged so the C++ side needs no edits. Verify each move with a build before starting the next.

**Gate (per concern, then final):** clean build; runtime smoke in a real session (launch, open/close a window, switch VT, multi-monitor if available) — this phase needs the compositor actually running, not just compiling.

---

## Phase 6: split Common.h

1,139 lines, 84 static-inline functions in the global namespace, one header included from most of the tree (exact include count to be measured with `grep -rl '#include.*Common.h'` at execution time).

**Split into focused headers under `src/utils/`:**
- `Arrays.h` — ArrayHeader, array_create/push/free (the overflow-guarded set from commit 19498cb)
- `Strings.h` — str_dup/concat/trim (overflow guards preserved)
- `Color.h` — Color struct, color_from_hex/lerp (null-guarded versions preserved)
- `MathUtils.h` — clamp/lerp
- `LogMacros.h` or fold into Logger — if any of the 84 functions are logging helpers
- Keep `Common.h` as a forwarding header including all of the above, so consumers need no edits in this phase; remove forwarding only if/when convenient

**Gate:** clean build (any missed include breaks loudly, which is the point of keeping Common.h as an umbrella first).

---

## Phase 7: docs reconciliation

Measured drift beyond what commit `f4a9481` already fixed (socket paths, D-Bus name, xwayland status):
- README/STATUS claim "40+ IPC commands" — actual 55 (post-Phase-1 number; recount then)
- "15 built-in plugins" — actual 14 built, 3 commented out
- README keybinding table says Meta+Return → foot — code tries alacritty first (Server.cpp:637)
- IPC_API.md: annotate/remove any commands dropped in Phase 1; add the removed `set_output_scale`/`set_zoom` as explicitly unsupported
- Add a one-paragraph "renderer architecture" note to README: wlroots renders windows via wlr_scene; custom overlay path is GLES2 (the post-48e70ea reality)

**Gate:** grep cross-checks — every documented IPC method exists in the registration table; every registration-table entry appears in IPC_API.md; plugin count in README matches `grep -c "Plugin.cpp" CMakeLists.txt` active entries.

---

## Execution rules

1. One phase per branch or working session; commit per phase (git skill rules: whole-file staging, no hunking)
2. A phase is done only when its gate passes with fresh command output recorded
3. Phases 1-4 and 6 are verifiable headless; Phase 5 (and IPC smoke of 1/2/4) needs a live session — flag those gates for manual runs
4. Stop rule: if any phase needs more than one repair cycle on its gate, stop and re-plan that phase instead of patch-and-pray
5. No new features ride along. This plan removes, moves, and makes honest — it does not add

## Expected end state

- ~47,600 source lines (from 53,338), all of it reachable
- wlr_bridge.c: ABI seam only (~800-1,000 lines est.)
- Server.cpp: composition root + event routing, no 55-lambda registration wall
- One window state owner
- IPC: every command either real or absent
- Docs matching measured reality
