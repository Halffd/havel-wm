# Implementation TODO - Continue Implementing Havel WM

## Priority Tasks

### P1: HotCorners Debounce Bug (CRITICAL)
- [ ] Fix HotCornersPlugin.cpp - `currentTime = 0` bug (line ~90)
- [ ] Add proper time source using getMonotonicTimeMs()

### P1: Overview Plugin Real Data
- [ ] Fix OverviewPlugin.cpp to use `m_api->getViewsInWorkspace(ws)` instead of fake data
- [ ] Use getViewAppId() and getViewTitle() when available

### P1: Window Metadata API
- [ ] Add getViewAppId() to CompositorAPI
- [ ] Add getViewTitle() to CompositorAPI
- [ ] Implement in Server class
- [ ] Query from XDG surface (appId from xdg_toplevel::app_id, title from xdg_toplevel::title)

### P2: App Launcher Shift/Special Char Handling
- [ ] Improve AppLauncherPlugin.cpp to use xkbcommon for proper character input
- [ ] Add shift modifier handling
- [ ] Add proper special character support

### P3: Alt-Tab Window Thumbnails
- [ ] Add texture capture for window thumbnails using wlr_scene_surface
- [ ] Connect to real window list (already done)
- [ ] Display actual window titles instead of placeholders

