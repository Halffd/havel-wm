# Havel WM - Comprehensive TODO List

**Generated:** 2026-03-29  
**Priority Legend:** 🔴 Critical | 🟠 High | 🟡 Medium | 🟢 Low | 🔵 Future

---

## 🔴 CRITICAL (Must Fix - Compositor Broken)

### Window Management
- [ ] **Window content not updating** - Foot terminal shows black screen with only caret
  - Suspect: Buffer commit signals not being processed
  - Suspect: Damage tracking not triggering redraws
  - Files: `wlr_bridge.c`, `src/wm/render/WlrBinding.c`

- [ ] **Input events escaping to other WMs** - Keybindings spawn windows in X11/Wayland
  - Suspect: `wlr_seat_keyboard_notify_key()` called even after consuming
  - Suspect: WAYLAND_DISPLAY not being set correctly
  - Files: `wlr_bridge.c:keyboard_handle_key()`

- [ ] **Close window crashes compositor (SIGIOT)**
  - Suspect: Use-after-free in view destroy handling
  - Suspect: C++ View destroyed while C still holds pointer
  - Files: `wlr_bridge.c:xdg_view_handle_destroy()`, `src/wm/core/Server.cpp:onViewDestroyed()`

- [ ] **Window resize not working** - Can move but not resize windows
  - Suspect: `xdg_handle_request_resize()` not updating geometry during drag
  - Files: `wlr_bridge.c:xdg_handle_request_resize()`

- [ ] **Window minimize/maximize buttons don't work** - ServerDecorationPlugin buttons click but don't act
  - Partial: Minimize now calls `havel_wlr_minimize_view()` but may not hide
  - Files: `src/wm/plugins/ServerDecorationPlugin.cpp`

### Rendering
- [ ] **Alt-Tab thumbnails not rendering** - Overlay shows but no window thumbnails
  - Fixed: Plugin overlay rendering now passes `OverlayRenderer*`
  - Remaining: `getViewTextureId()` returns 0 (stubbed)
  - Files: `src/wm/plugins/AltTabPlugin.cpp`, `src/wm/plugins/PluginManager.cpp`

- [ ] **Plugins not visible** - WorkspaceInfoBar, FPS, etc. don't render
  - Fixed: `PluginManager::renderOverlays()` now passes renderer
  - Remaining: Plugins may be checking for null or renderer not initialized
  - Files: `src/wm/plugins/PluginManager.cpp`, `src/wm/core/bridge.cpp`

- [ ] **Per-monitor workspace support** - All monitors show same workspace
  - Suspect: Single `server->scene` shared across outputs
  - Files: `wlr_bridge.c:server_new_output()`

---

## 🟠 HIGH (Important Features Missing)

### Plugin System
- [ ] **Plugin settings not loading from config** - `loadConfig()` called but settings ignored
  - Fixed: `PluginSettings` class exists
  - Remaining: Integration with `PluginConfig` JSON loader
  - Files: `src/wm/plugins/PluginManager.cpp:loadPluginSettings()`

- [ ] **Plugin event system not used** - `emitEvent()` exists but no plugins use it
  - Files: `src/wm/plugins/Plugin.hpp`, all plugins

- [ ] **Plugin priority ordering** - Plugins sorted but priority values all default (100)
  - Files: All plugins' `getInfo()` implementations

- [ ] **Plugin hot-reload incomplete** - `reloadConfig()` exists but doesn't enable/disable plugins
  - Files: `src/wm/plugins/PluginManager.cpp:reloadConfig()`

### Keybindings
- [ ] **Keybindings not working** - Meta+Return, Meta+D, etc. don't trigger
  - Debug logging added but root cause unknown
  - Suspect: `KeybindingManager::handleKey()` not matching modifiers
  - Suspect: Modifier mask mismatch (XKB vs wlroots)
  - Files: `src/wm/input/KeybindingManager.cpp`, `wlr_bridge.c:keyboard_handle_key()`

- [ ] **No keybinding configuration** - Hardcoded in `Server::registerKeybindings()`
  - Should load from `plugins.json` or `config.hv`
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
- [ ] **Touchpad gestures incomplete** - Only basic swipe recognized
  - TODO in code: "Add more complex gesture recognition (ZigZag, S-shape, Check)"
  - Files: `src/wm/input/GestureRecognizer.cpp:177`

- [ ] **No touch input support** - Touchscreen not handled
  - Files: `wlr_bridge.c`

- [ ] **No tablet input support** - Graphics tablets not handled
  - Files: `wlr_bridge.c`

### Output
- [ ] **HDR support incomplete** - Vulkan renderer has HDR code but not integrated
  - Files: `src/wm/render/VulkanRenderer.c`, `wlr_bridge.c`

- [ ] **No output profiles** - Can't save per-monitor configurations
  - Files: `src/wm/core/Server.cpp`

### Performance
- [ ] **No frame timing** - FPS plugin shows count but not frametimes
  - Files: `src/wm/plugins/FPSPlugin.cpp`

- [ ] **No memory tracking** - Can't detect leaks
  - Files: N/A

- [ ] **PipeWire uses memcpy** - Not zero-copy DMA-BUF
  - Note: Works fine, just not optimal
  - Files: `src/wm/core/PipeWireStream.cpp:204`

---

## 🟢 LOW (Nice to Have)

### Visual Polish
- [ ] **Animations** - Window open/close, workspace switch animations
  - `Animator` class exists but may not be used
  - Files: `src/wm/Animator.hpp`, `src/wm/core/Server.cpp`

- [ ] **Drop shadows** - Windows don't cast shadows
  - Would need: Shadow rendering pass or wlroots integration
  - Files: N/A

- [ ] **Rounded corners** - Windows have sharp corners
  - Would need: Shader or wlroots integration
  - Files: N/A

- [ ] **Blur behind transparent windows** - Kawase blur exists but may not auto-apply
  - Files: `src/wm/plugins/BlurPlugin.cpp`

### Plugins
- [ ] **Wobbly windows plugin** - Commented out in CMakeLists
  - Files: `src/wm/plugins/WobblyWindowsPlugin.cpp`

- [ ] **Destroy on close effect** - Commented out
  - Files: `src/wm/plugins/DestroyOnClosePlugin.cpp`

- [ ] **Spring on spawn effect** - Commented out
  - Files: `src/wm/plugins/SpringOnSpawnPlugin.cpp`

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
  - Would need: PipeWire remote or custom implementation
  - Files: N/A

- [ ] **Color management** - ICC profiles, color calibration
  - Files: N/A

- [ ] **HDR10+ / Dolby Vision** - Dynamic metadata
  - Partial: HDR10 PQ/HLG support exists
  - Files: `src/wm/render/VulkanRenderer.c`

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

### What's NOT Working
- ❌ Window buffers not updating (content black)
- ❌ Input events not consumed (escape to other WMs)
- ❌ Window close crashes
- ❌ Plugin textures not available (thumbnails)
- ❌ Keybindings not triggering

### Root Cause Hypothesis
The core issue is **event flow**:
1. Windows map but buffers don't commit → no content
2. Keys press but events not consumed → escape to X11
3. Windows destroy but pointers not cleared → crash

Fix event flow, and 80% of critical issues resolve.

---

**This TODO list is auto-generated from code analysis, TODO.md, and debugging sessions.**
**Last updated:** 2026-03-29
