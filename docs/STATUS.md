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

### ✅ Gamma Control (FIXED - COMBINED LUT)
- [x] Per-output gamma LUT application
- [x] Checks `wlr_output->gamma_size`
- [x] **COMBINED**: gamma_curve × brightness × kelvin_rgb
- [x] Single LUT upload (no stomping)
- [x] Temperature RGB multipliers (blackbody approx)
- [x] Brightness scalar multiplication
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

**ARCHITECTURE BUG:** Stores `void* viewPtr` → breaks abstraction layer

```cpp
// CURRENT (BREAKS ENCAPSULATION):
m_api->focusView((View*)win.viewPtr);  // ← Plugin sees View*

// SHOULD BE:
m_api->focusViewById(win.id);  // ← Opaque ID only
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

**To fix:** 
1. Add `focusViewById(uint64_t)` to CompositorAPI
2. Add `Server::getAllViews()` API

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

## Critical Architecture Bugs (Must Fix)

### 🔴 P0: Plugin Keycode Hardcoding

**Problem:** GammaPlugin, others use hardcoded keycodes

```cpp
// WRONG (US-only):
if (event.keycode == 104) {  // PageUp - breaks on non-US }

// RIGHT (use KeybindingManager):
keybindingManager.register("gamma.increase", MOD_LOGO | KEY_PAGEUP, [](){
    // ...
});
```

**Impact:** International layouts broken

**Fix:** Migrate all plugins to KeybindingManager

---

### 🔴 P0: View* Leak in Plugins

**Problem:** AltTab/Overview store raw `View*` pointers

```cpp
// WRONG (breaks encapsulation):
struct WindowEntry {
    void* viewPtr;  // ← Internal type leaked to plugin
};

// RIGHT (opaque ID):
struct WindowEntry {
    uint64_t viewId;  // ← Opaque handle
};
```

**Impact:** ABI break if View changes, plugin sees internals

**Fix:** 
1. Add `focusViewById(uint64_t)` to CompositorAPI
2. Plugins store IDs only

---

### 🟡 P1: GammaPlugin State Inconsistency (FIXED)

**Was:** `toggleNightMode()` didn't update `m_temperature`

**Fixed:** Now updates internal state before calling API

---

### 🟡 P1: Separate Gamma/Brightness/Temp LUTs (FIXED)

**Was:** Each setter uploaded separate LUT (stomping previous)

**Fixed:** Single combined LUT: `gamma_curve × brightness × kelvin_rgb`

---

### 🟡 P1: HotCorners Debounce Broken

**Problem:** `currentTime = 0` means debounce never triggers

```cpp
uint64_t currentTime = 0;  // ← Always zero!

// Should be:
uint64_t currentTime = getMonotonicTimeMs();
```

**Fix:** Add time source, fix debounce logic

---

## Priority Fixes

### P0 (Critical)
1. ✅ ~~Gamma LUT combination~~ DONE
2. ✅ ~~GammaPlugin state consistency~~ DONE
3. ⏳ KeybindingManager integration into Server
4. ⏳ Fix App Launcher to use xkbcommon
5. ⏳ Fix View* leak in AltTab/Overview plugins

### P1 (High)
6. Add `Server::getAllViews()` API
7. Fix Alt-Tab to use real window list
8. Fix Overview to use real workspace data
9. Fix HotCorners debounce/time source

### P2 (Medium)
10. Fix overlay render pass order
11. Add window texture capture
12. Add plugin configuration system

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
