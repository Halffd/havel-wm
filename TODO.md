# Havel WM - Comprehensive TODO List

**Generated:** 2026-03-29  
**Priority Legend:** 🔴 Critical | 🟠 High | 🟡 Medium | 🟢 Low | 🔵 Future

---

## 🔴 CRITICAL (Must Fix - Compositor Broken)

### Window Management
- [ ] **Window content not updating** - Foot terminal shows black screen with only caret
  - Suspect: Buffer commit signals not being processed
  - Suspect: Damage tracking not triggering redraws
  - Files: `wlr_bridge.c`

- [ ] **Input events escaping to other WMs** - Keybindings spawn windows in X11/Wayland
  - Suspect: `wlr_seat_keyboard_notify_key()` called even after consuming
  - Suspect: WAYLAND_DISPLAY not being set correctly
  - Files: `wlr_bridge.c:keyboard_handle_key()`

- [ ] **Close window crashes compositor (SIGIOT)**
  - Suspect: Use-after-free in view destroy handling
  - Suspect: C++ View destroyed while C still holds pointer
  - Root cause found (XWayland case, fixed): every cpp_impl_view_*
    callback in wlr_bridge.c cast the untyped nativeHandle to
    havel_xdg_view* unconditionally, but XWayland views are
    havel_xwayland_view — closing/focusing/fullscreening any X11 window
    dereferenced a reinterpreted struct (garbage ->toplevel pointer
    into wlr_xdg_toplevel_send_close). fixed with a havel_view_kind
    tag (first member of both wrapper structs) + per-kind dispatch;
    close/fullscreen/geometry now call the wlr_xwayland_surface APIs.
    the XDG path suspects above may still apply for XDG windows — needs
    the live-session crash repro to confirm.
  - Files: `wlr_bridge.c:xdg_view_handle_destroy()`, `src/wm/core/Server.cpp:onViewDestroyed()`

- [ ] **Window resize not working** - Can move but not resize windows
  - Suspect: `xdg_handle_request_resize()` not updating geometry during drag
  - Files: `wlr_bridge.c:xdg_handle_request_resize()`

- [x] **Window minimize/maximize buttons don't work** - ServerDecorationPlugin buttons click but don't act
  - Fixed: minimize passed the C++ View* straight to the C bridge (which
    expects the C-side wrapper struct — wrong pointer type, undefined
    behavior); now routes through new CompositorAPI::minimizeView() ->
    PluginManager -> nativeHandle -> havel_wlr_minimize_view, same as the
    close path. maximize previously only set geometry without real window
    state; now uses CompositorAPI::toggleMaximizeView() ->
    CoreWindowManager::toggleMaximize.
  - Remaining: visual verification in a live session
  - Files: `src/wm/plugins/ServerDecorationPlugin.cpp`

### Rendering
- [ ] **Alt-Tab thumbnails not rendering** - Overlay draws (scene-backed
  since f9119a2), but window thumbnails are placeholder rects
  - The GL overlay path could never render (no context on the wlroots
    0.20 frame path); replaced with the scene-graph renderer 2026-09
  - Remaining: real view capture. `havel_wlr_capture_view_texture` is an
    honest 0-returning stub (GL texture capture has no wlroots 0.20
    API). Would need a scene-based capture (e.g. render view subtree to
    a CPU wlr_buffer, same technique as SceneText) — icons already work
    this way via CPU pixels.
  - Files: `wlr_bridge.c:havel_wlr_capture_view_texture()`, `src/wm/plugins/AltTabPlugin.cpp`

- [x] ~~**Plugins not visible** - WorkspaceInfoBar, FPS, etc. don't render~~
  FIXED 2026-09 (f9119a2) - the overlay renderer issued raw GLES2 calls
  outside any render pass (structurally impossible under wlroots 0.20);
  rewritten on the scene graph: rects via wlr_scene_rect, text via
  FreeType->CPU buffers, icons via pixel blits. composites through the
  same build_state path as windows.
  - Remaining: live-session pixel verification
  - Files: `src/wm/render/OverlayRenderer.cpp`

- [ ] **Per-monitor workspace support** - All monitors show same workspace
  - Suspect: Single `server->scene` shared across outputs
  - Files: `wlr_bridge.c:server_new_output()`

---

## 🟠 HIGH (Important Features Missing)

### Plugin System
- [x] **Plugin settings not loading from config** - `loadConfig()` called but settings ignored
  - Fixed in `d63baa4`: `loadPluginSettings()` now calls each plugin's virtual
    `loadConfig()` (reads via PluginConfig::getInstance), the whitelist
    extraction is gone, and `registerPlugin` loads settings before the plugin
    enters the enabled set — first boot applies user config.
  - Files: `src/wm/plugins/PluginManager.cpp:loadPluginSettings()`

- [ ] **Plugin event system not used** - `emitEvent()` exists but no plugins use it
  - Files: `src/wm/plugins/Plugin.hpp`, all plugins

- [ ] **Plugin priority ordering** - Plugins sorted but priority values all default (100)
  - Files: All plugins' `getInfo()` implementations

- [ ] **Plugin hot-reload incomplete** - `reloadConfig()` exists but doesn't enable/disable plugins
  - Files: `src/wm/plugins/PluginManager.cpp:reloadConfig()`

### Keybindings
- [x] ~~**Keybindings not working**~~ FIXED 2026-09 (commit 69d23d1) - root
  cause found: the bridge passes keycode+8 (xkb convention) but every
  keycode literal was raw evdev, so most bindings fired on the wrong
  physical key or never; six plugins also defined MOD_SHIFT as 1<<1
  which is CapsLock. all matching is keysym-based now (layout-aware).
  Remaining risk is live-session verification only.
  - Files: `src/wm/input/KeybindingManager.cpp`, `wlr_bridge.c:keyboard_handle_key()`

- [ ] **Server keybindings not configurable** - plugin toggle bindings are
  configurable via `plugins.json` "keybinding" (since 08fb7b8), but the
  ~30 Server-level bindings (workspace switch, fullscreen, etc.) remain
  hardcoded in `Server::registerKeybindings()`
  - Files: `src/wm/core/Server.cpp`

### Window Management
- [ ] **No window decorations by default** - ServerDecorationPlugin exists but may not auto-apply
  - Files: `src/wm/plugins/ServerDecorationPlugin.cpp`

- [ ] **No window rules** - Can't force apps to specific workspaces or floating
  - Mentioned in `config.hv.example` but not implemented
  - Files: `src/wm/core/Server.cpp`, `src/wm/core/CoreWindowManager.cpp`

- [ ] **No drag-to-move from titlebar** - Only Meta+drag works
  - Files: `src/wm/plugins/ServerDecorationPlugin.cpp:onMouseButton()`

### Configuration
- [ ] **No Havel config parser** - `config.hv.example` exists but no parser
  - Decided: JSON is sufficient, Havel parser over-engineering
  - Files: N/A (keep `plugins.json` only)

- [ ] **Configuration hot-reload doesn't apply all settings** - Only reloads JSON, doesn't reconfigure plugins
  - Files: `src/wm/plugins/PluginManager.cpp:reloadConfig()`

---

## 🟡 MEDIUM (Quality of Life)

### Plugin Improvements
- [ ] **Wallpaper plugin only does solid color** - No image support
  - Would need: Image loading (stb_image or similar), texture upload
  - Files: `src/wm/plugins/WallpaperPlugin.cpp`

- [ ] **App Launcher doesn't launch apps** - Shows apps but can't spawn
  - Files: `src/wm/plugins/AppLauncherPlugin.cpp:onKey()`

- [ ] **Scale plugin highlight uses hardcoded color** - Should use config
  - Partial: Now uses `m_highlightColor` from config
  - Remaining: Test it works
  - Files: `src/wm/plugins/ScalePlugin.cpp:highlightSelected()`

- [ ] **Alt-Tab doesn't select window on Alt release** - Only on Enter
  - Files: `src/wm/plugins/AltTabPlugin.cpp:onKey()`

- [ ] **Draw plugin strokes not persisted** - Cleared on redraw
  - Files: `src/wm/plugins/DrawPlugin.cpp`

- [ ] **FPS plugin not rendering** - Returns early or not enabled
  - Files: `src/wm/plugins/FPSPlugin.cpp`

- [ ] **Zoom plugin not implemented** - Stub only
  - Files: `src/wm/plugins/ZoomPlugin.cpp`

- [ ] **Workspace info bar not rendering** - Plugin exists but not visible
  - Files: `src/wm/plugins/WorkspaceInfoBar.cpp`

### Input
- [x] ~~**Touchpad gestures incomplete**~~ REMOVED 2026-09 - GestureRecognizer/
  GestureManager (and ComboManager, InputDeviceManager) were never instantiated
  and registerGesture had zero callers; the wired chain fed an empty action list.
  Deleted with the bridge wiring. Re-derive from wlroots touch events when a
  real consumer exists.
  - Files: N/A (deleted)

- [ ] **No touch input support** - Touchscreen not handled
  - Files: `wlr_bridge.c`

- [ ] **No tablet input support** - Graphics tablets not handled
  - Files: `wlr_bridge.c`

### Output
- [ ] **No HDR support** - custom HDR renderer code was removed with the dead renderer paths; wlroots autocreate renderer is SDR-only for now
  - Files: `wlr_bridge.c`

- [ ] **No output profiles** - Can't save per-monitor configurations
  - Files: `src/wm/core/Server.cpp`

### Performance
- [ ] **No frame timing** - FPS plugin shows count but not frametimes
  - Files: `src/wm/plugins/FPSPlugin.cpp`

- [ ] **No memory tracking** - Can't detect leaks
  - Files: N/A

- [x] ~~**PipeWire uses memcpy**~~ REMOVED 2026-09 - PipeWireStream never
  ran (see screen-capture removal); screencopy protocol replaced it. If a
  PipeWire path is ever wanted, xdg-desktop-portal-wlr is the supported route.

---

## 🟢 LOW (Nice to Have)

### Visual Polish
- [ ] **Animations** - Window open/close, workspace switch animations
  - `Animator` class exists but may not be used
  - Files: `src/wm/include/wm/Animator.hpp`, `src/wm/core/Server.cpp`

- [ ] **Drop shadows** - Windows don't cast shadows
  - Would need: Shadow rendering pass or wlroots integration
  - Files: N/A

- [ ] **Rounded corners** - Windows have sharp corners
  - Would need: Shader or wlroots integration
  - Files: N/A

- [ ] **Blur behind transparent windows** - No blur plugin exists
  (BlurPlugin.cpp never existed; the old factory declaration was removed
  2026-09. Kawase blur would need a scene-compatible implementation)
  - Files: N/A

### Plugins
- [x] ~~**Wobbly windows plugin**~~ REMOVED 2026-09 - GL-coupled (VBO/shader
  animation), unbuildable since the scene-graph migration; delete recorded here
- [x] ~~**Destroy on close effect**~~ REMOVED 2026-09 - GL particle members,
  same story
- [x] ~~**Spring on spawn effect**~~ REMOVED 2026-09 - same story; re-derive
  from the scene API if wanted (see OverlayRenderer for the pattern)

- [ ] **Magic lamp effect** - Minimize animation (like macOS)
  - Files: Not created

### Integration
- [ ] **System tray** - No status area for icons
  - Would need: SNI (Status Notifier Item) support
  - Files: N/A

- [ ] **Clipboard manager** - No history, just current selection
  - Files: N/A

- [ ] **Screen lock integration** - `havel-lock` exists but may not integrate
  - Files: `src/wm/core/LoadingScreen.c`, `havel-lock/`

- [ ] **Polkit agent** - No authentication dialogs
  - Would need: External agent or built-in
  - Files: N/A

### Documentation
- [ ] **Man pages** - No `man havel-wm`
  - Files: N/A

- [ ] **Configuration guide** - `plugins.json.example` exists but no docs
  - Files: `plugins.json.example`

- [ ] **Plugin development guide** - How to write new plugins
  - Files: N/A

---

## 🔵 FUTURE (Next Version)

### Level 3 Plugin System
- [ ] **Runtime plugin loading** - Currently Level 2 (compiled-in)
  - Would need: `dlopen()` integration, plugin ABI
  - Files: `src/wm/plugins/PluginManager.hpp` (mentions Level 3)

- [ ] **Plugin repository** - Download/install plugins
  - Files: N/A

- [ ] **Plugin sandboxing** - Isolate plugins from compositor
  - Files: N/A

### Advanced Features
- [ ] **Remote desktop** - RDP/VNC server
  - Would need: custom implementation (or wayvnc, which builds on the
    wlr protocols the compositor already creates)
  - Files: N/A

- [ ] **Color management** - ICC profiles, color calibration
  - Files: N/A

- [ ] **HDR10+ / Dolby Vision** - Dynamic metadata
  - Removed with the dead Vulkan renderer paths; would need a new design on top of wlroots' renderer
  - Files: N/A

- [ ] **Variable refresh rate** - VRR/FreeSync/G-Sync
  - Would need: wlroots support
  - Files: N/A

- [ ] **Multi-GPU** - Different apps on different GPUs
  - Note: `MultiGPU.c/h` was deleted (300 lines of stubs)
  - Files: N/A

### Wayland Protocols
- [ ] **wlr-virtual-pointer-v1** - Remote control
- [ ] **ext-session-lock-v1** - Modern lock protocol
- [ ] **wlr-output-management-v1** - Advanced output config
- [ ] **wlr-output-power-management-v1** - DPMS

### Havel Language Integration
- [ ] **Execute Havel scripts** - Run `.hv` files as compositor commands
  - Note: Havel parser exists in `havel/src/havel-lang/`
  - Files: `havel/src/havel-lang/parser/Parser.cpp`

- [ ] **Havel keybindings** - Define keybindings in Havel
  - Example: `on Meta+Return { spawn("foot") }`
  - Files: N/A

- [ ] **Havel plugins** - Write plugins in Havel
  - Would need: Havel FFI, plugin API bindings
  - Files: N/A

---

## 📊 Summary by Category

| Category | Critical | High | Medium | Low | Future | Total |
|----------|----------|------|--------|-----|--------|-------|
| Window Management | 5 | 3 | 1 | 0 | 0 | 9 |
| Rendering | 3 | 0 | 0 | 4 | 2 | 9 |
| Plugin System | 0 | 4 | 8 | 4 | 3 | 19 |
| Input | 1 | 1 | 3 | 0 | 0 | 5 |
| Configuration | 0 | 1 | 1 | 1 | 2 | 5 |
| Output/Display | 1 | 0 | 1 | 0 | 2 | 4 |
| Performance | 0 | 0 | 3 | 0 | 0 | 3 |
| Integration | 0 | 0 | 0 | 4 | 2 | 6 |
| Documentation | 0 | 0 | 0 | 3 | 0 | 3 |
| Havel Language | 0 | 0 | 0 | 0 | 3 | 3 |
| **TOTAL** | **10** | **9** | **17** | **16** | **14** | **66** |

---

## 🎯 Recommended Priority Order

### Phase 1: Make It Usable (Weeks 1-2)
1. Fix window content updating (black screen)
2. Fix input event consumption (keybindings)
3. Fix close crash (SIGIOT)
4. Fix window resize
5. Get Alt-Tab thumbnails working

### Phase 2: Make It Functional (Weeks 3-4)
6. Get plugins rendering (FPS, Workspace bar, etc.)
7. Fix keybinding configuration
8. Implement plugin settings loading
9. Get App Launcher spawning apps
10. Fix minimize/maximize buttons

### Phase 3: Make It Polished (Weeks 5-6)
11. Window rules (per-app workspace, floating)
12. Drag-to-move from titlebar
13. Plugin hot-reload
14. Touchpad gestures
15. Performance metrics

### Phase 4: Make It Pretty (Weeks 7-8)
16. Animations
17. Wallpaper images
18. Blur behind transparent windows
19. Drop shadows
20. Rounded corners

---

## 📝 Notes

### What's Actually Working
- ✅ wlroots rendering (proven by red box test)
- ✅ Windows ARE created (foot spawns)
- ✅ Scene graph structure exists
- ✅ Plugin infrastructure works
- ✅ Overlay rendering pipeline works
- ✅ Keybinding system exists
- ✅ Configuration loading works

### What's NOT Working (2026-09 update)
- ❌ Window buffers not updating (content black) — live-session diagnosis
  pending; loading-screen stub (auto-hide) and overlay GL path both
  eliminated as suspects this cycle
- ❌ Input events not consumed (escape to other WMs) — keysym dispatch is
  correct by construction now; needs live confirmation
- ❌ Window close crashes — XWayland kind-tag fix landed; XDG path still
  needs live-session repro
- ❌ Plugin textures not available (thumbnails) — needs scene-based
  capture (see Rendering section)
- ✅ Keybindings triggering — keycode/keysym mismatch root-caused and
  fixed (69d23d1); one-owner-per-key deconflicted (002d6af); plugin
  bindings configurable (08fb7b8)

### Root Cause Hypothesis (2026-09 status)
The original "event flow" hypothesis was half right: keys DID flow, but
every keycode literal was written in the wrong convention (raw evdev vs
the bridge's xkb keycode+8) so handlers matched the wrong keys. That is
fixed. Window buffers are the remaining live-session question.

---

**This TODO list is auto-generated from code analysis, TODO.md, and debugging sessions.**
**Last updated:** 2026-09 (post 2026-09 audit: keysym input, scene overlays,
dead-layer removal; see git log 69d23d1..9da2c16)
