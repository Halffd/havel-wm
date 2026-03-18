# Havel WM - Complete Roadmap

**Generated:** 2026-03-17  
**Total Admissions of Incompleteness:** 296  
**Codebase:** ~100k lines

---

## 🔴 P0 - CRITICAL (Core Functionality Broken)

### 1. Window Movement & Resizing
**Status:** ❌ Broken  
**Issue:** Windows can't be moved or resized  
**Files:** `wlr_bridge.c`, `Server.cpp`  
**Lines Affected:** ~15

```
// Current state:
wlr_scene_node_set_position(&view->scene_tree->node, x, y);  // Doesn't work
```

**TODO:**
- [ ] Fix wlroots 0.20 scene graph position updates
- [ ] Implement proper resize handling
- [ ] Test with XDG and XWayland windows

**Estimate:** 8 hours

---

### 2. Keyboard Input / Text Entry
**Status:** ❌ Broken  
**Issue:** Can't type in terminal  
**Files:** `wlr_bridge.c`, `Server.cpp`, `InputManager.cpp`  
**Lines Affected:** ~25

```
// Current state:
// Key events received but text input not working
```

**TODO:**
- [ ] Fix text-input-v3 protocol integration
- [ ] Implement IME passthrough
- [ ] Test with foot, alacritty, VSCode

**Estimate:** 12 hours

---

### 3. Black Screen / No Content
**Status:** ❌ Broken  
**Issue:** Compositor shows black background only  
**Files:** `wlr_bridge.c`, `Server.cpp`  
**Lines Affected:** ~20

```
// Current state:
// Windows map but don't render
```

**TODO:**
- [ ] Fix surface commit handling
- [ ] Verify buffer attachment
- [ ] Check output frame callbacks

**Estimate:** 6 hours

---

## 🟠 P1 - HIGH (Core Features Missing)

### 4. Tiling Window Manager
**Status:** ⚠️ Partial  
**Issue:** Basic tiling exists but incomplete  
**Files:** `Layout.cpp`, `Server.cpp`, `Workspace.cpp`  
**Lines Affected:** ~50

**TODO:**
- [ ] Fix master-stack layout
- [ ] Implement gap configuration
- [ ] Add layout switching (horizontal/vertical/grid)
- [ ] Window swapping in layout
- [ ] Persistent layout per workspace

**Estimate:** 16 hours

---

### 5. Blur Effects
**Status:** ⚠️ Partial (5 implementations, 0 working)  
**Files:** `BlurPlugin.cpp`, `BlurShader.cpp`, `RenderPipeline.cpp`  
**Lines Affected:** ~600

```
// Current state:
// BlurPlugin exists, BlurShader compiled, but never called
// "Would apply blur" × 15
```

**TODO:**
- [ ] Connect BlurShader to render pipeline
- [ ] Implement FBO capture
- [ ] Add blur configuration (radius, passes)
- [ ] Delete 4 dead blur implementations

**Estimate:** 12 hours

---

### 6. Vulkan Renderer
**Status:** ⚠️ Stub  
**Files:** `VulkanRenderer.cpp`, `VulkanSceneCompositor.c`  
**Lines Affected:** ~1000

```
// Current state:
// "Placeholder - would need actual texture upload"
// "Swap chain is out of date, would need recreation"
```

**TODO:**
- [ ] Complete texture upload implementation
- [ ] Fix swapchain recreation
- [ ] Integrate with wlroots scene graph
- [ ] OR: Delete and use GLES2 only

**Estimate:** 40 hours (or 2 hours to delete)

---

### 7. Screen Capture / PipeWire
**Status:** ⚠️ Stub  
**Files:** `ScreenCapture.cpp`, `PipeWireStream.cpp`, `DesktopPortal.cpp`  
**Lines Affected:** ~350

```
// Current state:
// "For now, stub implementation"
// "In production, would use DMA-BUF"
```

**TODO:**
- [ ] Complete PipeWire stream integration
- [ ] Implement DMA-BUF zero-copy
- [ ] Add xdg-desktop-portal integration
- [ ] Test with Firefox, Chrome screen sharing

**Estimate:** 20 hours

---

## 🟡 P2 - MEDIUM (Quality of Life)

### 8. Gesture Recognition
**Status:** ⚠️ Basic  
**Files:** `GestureRecognizer.cpp`  
**Lines Affected:** ~20

```
// Current state:
// TODO: Add more complex gesture recognition (ZigZag, S-shape, Check)
```

**TODO:**
- [ ] Implement ZigZag gesture
- [ ] Implement S-shape gesture
- [ ] Implement Check gesture
- [ ] Add gesture configuration

**Estimate:** 8 hours

---

### 9. Scene Graph Container Operations
**Status:** ⚠️ Incomplete  
**Files:** `SceneGraphOps.cpp`  
**Lines Affected:** ~50

```
// Current state:
// TODO: Add to container
// TODO: Remove from container
// TODO: Move children to sibling container
```

**TODO:**
- [ ] Implement container view add/remove
- [ ] Implement container nesting
- [ ] Implement container destruction with child migration

**Estimate:** 12 hours

---

### 10. Layout Persistence
**Status:** ⚠️ Save only, no load  
**Files:** `SceneGraphOps.cpp`  
**Lines Affected:** ~20

```
// Current state:
// TODO: Implement JSON parsing for layout restore
```

**TODO:**
- [ ] Implement JSON parser (or use cJSON)
- [ ] Restore container hierarchy
- [ ] Match views by app_id/title
- [ ] Restore floating positions

**Estimate:** 8 hours

---

### 11. Wallpaper Plugin
**Status:** ❌ Stub  
**Files:** `WallpaperPlugin.cpp`  
**Lines Affected:** ~60

```
// Current state:
// "This stub shows the plugin structure"
// "Actual implementation requires:"
```

**TODO:**
- [ ] Load wallpaper image
- [ ] Support multiple outputs
- [ ] Support solid color
- [ ] Support slideshow
- [ ] Add configuration

**Estimate:** 12 hours

---

### 12. Server Decorations
**Status:** ⚠️ Partial  
**Files:** `ServerDecorationPlugin.cpp`  
**Lines Affected:** ~40

```
// Current state:
// "Minimize would require compositor support"
// "For now, just log"
```

**TODO:**
- [ ] Implement minimize (hide from scene graph)
- [ ] Implement maximize animation
- [ ] Add close button functionality
- [ ] Add window drag/move via title bar

**Estimate:** 16 hours

---

## 🟢 P3 - LOW (Nice to Have)

### 13. Wobbly Windows
**Status:** ⚠️ Debug only  
**Files:** `WobblyWindowsPlugin.cpp`  
**Lines Affected:** ~30

```
// Current state:
// "Would render debug visualization"
// "For production, would use deformed mesh"
```

**TODO:**
- [ ] Complete mesh deformation
- [ ] Integrate with actual window rendering
- [ ] Add configuration (spring, damping)
- [ ] OR: Delete if not worth it

**Estimate:** 20 hours (or 1 hour to delete)

---

### 14. Advanced Shader Effects
**Status:** ❌ Unused  
**Files:** `AdvancedShaderEffects.cpp` (DELETED)  
**Lines Affected:** 0 (already deleted)

```
// Current state:
// File deleted - 8 effects never rendered
```

**TODO:**
- [ ] N/A - Already cleaned up

**Estimate:** 0 hours ✅

---

### 15. Plugin System Level 3
**Status:** ❌ Future  
**Files:** `PluginManager.hpp`  
**Lines Affected:** ~5

```
// Current state:
// "Future: Level 3 with dlopen() for runtime loading"
```

**TODO:**
- [ ] Implement dlopen() plugin loading
- [ ] Add plugin discovery
- [ ] Add plugin hot-reload
- [ ] Add plugin dependency management

**Estimate:** 24 hours

---

### 16. Hot Corners Visual Feedback
**Status:** ❌ Missing  
**Files:** `HotCornersPlugin.cpp`  
**Lines Affected:** ~10

```
// Current state:
// "- Visual feedback (future)"
```

**TODO:**
- [ ] Add corner highlight on hover
- [ ] Add animation
- [ ] Add configuration

**Estimate:** 4 hours

---

### 17. Custom Layouts Plugin
**Status:** ⚠️ Partial  
**Files:** `CustomLayoutsPlugin.cpp`  
**Lines Affected:** ~20

```
// Current state:
// "Would load layout preferences, gaps, ratios"
// "In production, would swap focused view with master"
```

**TODO:**
- [ ] Implement master swap
- [ ] Add layout configuration
- [ ] Add gap configuration
- [ ] Add per-workspace layouts

**Estimate:** 12 hours

---

### 18. Window Snap Plugin
**Status:** ⚠️ Partial  
**Files:** `WindowSnapPlugin.cpp`  
**Lines Affected:** ~15

```
// Current state:
// "Would restore to previous size/position"
// "For now, just log"
```

**TODO:**
- [ ] Implement geometry history
- [ ] Implement restore functionality
- [ ] Add snap threshold configuration
- [ ] Add visual snap indicator

**Estimate:** 8 hours

---

### 19. Gamma Plugin Night Mode
**Status:** ⚠️ Partial  
**Files:** `GammaPlugin.cpp`  
**Lines Affected:** ~10

```
// Current state:
// "Would load gamma, temperature, brightness, night mode schedule"
```

**TODO:**
- [ ] Add night mode schedule
- [ ] Add location-based sunset/sunrise
- [ ] Add smooth transitions
- [ ] Add configuration UI

**Estimate:** 8 hours

---

### 20. App Launcher Plugin
**Status:** ⚠️ Partial  
**Files:** `AppLauncherPlugin.cpp`  
**Lines Affected:** ~15

```
// Current state:
// "Would load custom search paths, favorites, etc."
// "Simple substring match (fuzzy would use Levenshtein)"
```

**TODO:**
- [ ] Implement fuzzy search (Levenshtein)
- [ ] Add custom search paths
- [ ] Add favorites/pinned apps
- [ ] Add app icons
- [ ] Add recent apps

**Estimate:** 12 hours

---

## 📊 Summary by Priority

| Priority | Items | Total Estimate |
|----------|-------|----------------|
| 🔴 P0 - Critical | 3 | 26 hours |
| 🟠 P1 - High | 4 | 90 hours |
| 🟡 P2 - Medium | 5 | 56 hours |
| 🟢 P3 - Low | 8 | 88 hours |
| **TOTAL** | **20** | **260 hours** |

---

## 📅 Timeline Estimates

| Scenario | Hours/Week | Completion |
|----------|------------|------------|
| **Solo, nights/weekends** | 10 hrs | 6 months |
| **Solo, full-time** | 40 hrs | 6-7 weeks |
| **2 developers** | 80 hrs | 3-4 weeks |
| **4 developers** | 160 hrs | 1.5-2 weeks |

---

## 🎯 Recommended Order

### Phase 1: Make It Work (P0)
1. Window Movement & Resizing
2. Keyboard Input / Text Entry
3. Black Screen / No Content

**Goal:** Basic compositor functionality

### Phase 2: Make It Usable (P1)
4. Tiling Window Manager
5. Blur Effects (or delete)
6. Screen Capture / PipeWire
7. Vulkan Renderer (or delete)

**Goal:** Daily driver ready

### Phase 3: Make It Nice (P2)
8. Gesture Recognition
9. Scene Graph Container Operations
10. Layout Persistence
11. Wallpaper Plugin
12. Server Decorations

**Goal:** Polished experience

### Phase 4: Make It Shine (P3)
13-20. All remaining features

**Goal:** Feature-complete

---

## ⚠️ Known Time Sinks

1. **Vulkan Renderer** - 40 hours to complete OR 2 hours to delete
2. **Wobbly Windows** - 20 hours to complete OR 1 hour to delete
3. **Blur Effects** - 5 implementations exist, 0 work. Pick one or delete all.
4. **Plugin System Level 3** - Nice to have, not critical

---

## ✅ Already Complete

- [x] Scene graph core (with validation)
- [x] Automatic view creation
- [x] Tiling integration
- [x] Layout persistence (save)
- [x] Enhanced IPC (JSON-RPC 2.0)
- [x] Per-monitor brightness/gamma/zoom
- [x] Alt-tab with 500px thumbnails
- [x] Dead code cleanup (BlurPlugin_v2, FPSPlugin_v2, AdvancedShaderEffects)

---

## 📝 Notes

- **296 admissions** across ~100k lines = 1 every ~338 lines
- **"WOULD"** is the biggest offender (192 occurrences)
- **"FOR NOW"** is permanent (59 occurrences)
- **TODO count (6)** is suspiciously low because most are disguised as WOULD

---

**Generated from actual codebase analysis.**  
**No estimates were harmed in the making of this roadmap.** 🗿
