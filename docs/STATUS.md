# Havel WM - Honest Status Report

**Date:** 2026-02-27  
**Version:** Development

---

## What's REAL (Production Ready)

### ✅ Core Compositor
- [x] wlroots backend initialization
- [x] Output management (multi-monitor)
- [x] Input handling (keyboard, pointer)
- [x] XDG shell window management
- [x] Scene graph rendering
- [x] Workspace system (10 workspaces)
- [x] Focus management

### ✅ Plugin System
- [x] Plugin interface (init/fini/events)
- [x] PluginManager with lifecycle
- [x] CompositorAPI abstraction
- [x] 12 plugins loaded

### ✅ Gamma Control (FIXED)
- [x] Per-output gamma LUT application
- [x] Checks `wlr_output->gamma_size`
- [x] Builds proper R/G/B ramps
- [x] Calls `wlr_output_set_gamma()`
- [x] Warns if output doesn't support gamma
- [x] Meta+W cycles wallpaper colors

### ✅ Overlay Rendering Infrastructure
- [x] GLES2 overlay renderer
- [x] FreeType text rendering
- [x] Rectangle/border drawing
- [x] Texture rendering
- [x] Alpha blending

### ✅ Keybinding System (NEW)
- [x] Central KeybindingManager
- [x] Duplicate detection
- [x] Conflict prevention
- [x] Registration API

---

## What's STUBBED (Works But Incomplete)

### ⏳ Alt-Tab Plugin
**Status:** UI works, window list is FAKE

```cpp
// CURRENT (FAKE):
WindowEntry term;
term.appId = "foot";
term.title = "Terminal";
m_windows.push_back(term);

// NEEDS:
auto views = server->getAllViews();
for (auto* view : views) {
    // Collect real windows
}
```

**What works:**
- Overlay renders
- Keyboard navigation
- Selection highlighting
- Text rendering

**What's fake:**
- Window list is hardcoded
- No actual window switching
- No focus tracking

**To fix:** Need `Server::getAllViews()` API

---

### ⏳ Overview Plugin
**Status:** UI works, workspace data is FAKE

```cpp
// CURRENT (FAKE):
for (uint32_t ws = 0; ws < WORKSPACE_COUNT; ws++) {
    OverviewWorkspace ows;
    ows.id = ws;
    // No actual window collection
    m_workspaces.push_back(ows);
}

// NEEDS:
for (auto* ws : server->getWorkspaces()) {
    ows.windows = ws->getViews();
}
```

**What works:**
- Grid layout rendering
- Keyboard navigation
- Workspace selection

**What's fake:**
- Window counts are zero
- No actual window data

**To fix:** Need workspace view enumeration

---

### ⏳ App Launcher Plugin
**Status:** UI works, input is BROKEN

```cpp
// CURRENT (BROKEN - hardcoded US keycodes):
static const char keymap[] = {
    0, 0, '1', '2', '3', ...  // US layout only
};

// NEEDS (use xkbcommon):
struct xkb_state* xkb_state = seat->xkb_state;
xkb_keysym_t keysym = xkb_state_key_get_one_sym(xkb_state, keycode);
char keysym_name[256];
xkb_keysym_get_name(keysym, keysym_name, sizeof(keysym_name));
```

**What works:**
- Search box UI
- Results list rendering
- Fuzzy matching logic
- App launching (stub)

**What's BROKEN:**
- Only works with US keyboard layout
- Shift/caps don't work
- International layouts broken

**To fix:** Use xkbcommon (already linked!)

---

### ⏳ Overlay Render Order
**Status:** Renders but may be wrong place

```c
// CURRENT (in wlr_bridge.c:output_frame):
wlr_scene_output_commit(output->scene_output, &options);
havel_render_pipeline_draw_overlays(...);  // ← After commit?

// SHOULD BE:
// 1. Begin render pass
// 2. Render scene to FBO
// 3. Render overlays on top
// 4. Commit
```

**Risk:** May cause tearing or wrong z-order

**To fix:** Integrate with proper render pass

---

## What's MISSING

### ❌ Real Window Enumeration
No API to get all views from Server.

**Need:**
```cpp
class Server {
    std::vector<View*> getAllViews() const;
    View* getFocusedView() const;
};
```

### ❌ xkbcommon Text Input
Hardcoded keycode→char mapping.

**Need:**
```cpp
class TextInput {
    xkb_state* m_state;
    char getKeyChar(uint32_t keycode);
};
```

### ❌ Window Texture Capture
Alt-Tab shows colored rects, not thumbnails.

**Need:**
- wlr_scene_surface capture
- Texture upload
- Mipmap generation

### ❌ Plugin Configuration
No way to disable/reconfigure plugins.

**Need:**
- Config file format
- Plugin enable/disable
- Keybinding remapping

---

## Priority Fixes

### P0 (Critical)
1. ✅ ~~Gamma LUT application~~ DONE
2. ⏳ KeybindingManager integration into Server
3. ⏳ Fix App Launcher to use xkbcommon

### P1 (High)
4. Add `Server::getAllViews()` API
5. Fix Alt-Tab to use real window list
6. Fix Overview to use real workspace data

### P2 (Medium)
7. Fix overlay render pass order
8. Add window texture capture
9. Add plugin configuration system

---

## Plugin Count: 12

| # | Plugin | Status | Notes |
|---|--------|--------|-------|
| 1 | Example | ✅ Real | Logs key events |
| 2 | Blur | ⏳ Stub | No shader yet |
| 3 | Scale | ⏳ Stub | No scene transform |
| 4 | Wallpaper | ✅ Real | Changes bg color |
| 5 | Notifications | ⏳ Stub | No UI rendering |
| 6 | Custom Layouts | ⏳ Stub | No layout engine |
| 7 | Window Snap | ⏳ Stub | No drag tracking |
| 8 | Hot Corners | ⏳ Stub | No cursor tracking |
| 9 | Gamma | ✅ Real | LUT applied |
| 10 | App Launcher | ⏳ Stub | UI works, input broken |
| 11 | Alt-Tab | ⏳ Stub | UI works, no real windows |
| 12 | Overview | ⏳ Stub | UI works, no real data |

**Real:** 3/12 (25%)  
**Stubbed:** 9/12 (75%)

---

## Code Stats

| Component | Lines | Status |
|-----------|-------|--------|
| Core WM | ~1,500 | ✅ Complete |
| Plugin System | ~400 | ✅ Complete |
| Overlay Rendering | ~600 | ✅ Infrastructure |
| FreeType Font | ~340 | ✅ Complete |
| KeybindingManager | ~120 | ✅ Complete |
| Plugins (12 total) | ~2,500 | ⏳ 25% real |
| **Total** | **~5,500** | |

---

## Summary

**What we have:**
- Solid compositor foundation
- Working plugin architecture
- Real gamma control
- Overlay rendering infrastructure
- Central keybinding system

**What we don't have:**
- Real window enumeration
- Proper text input (xkb)
- Window texture capture
- Plugin configuration

**Honest assessment:**
The compositor is **architecturally complete** but **feature-incomplete**. The foundation is solid—what's needed now is connecting the stubs to real compositor state.

**Next sprint priorities:**
1. Integrate KeybindingManager
2. Fix App Launcher input (xkb)
3. Add `getAllViews()` API
4. Connect Alt-Tab to real windows
