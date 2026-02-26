# Havel WM Plugin System

## Overview

Havel WM uses a **Level 2 plugin system** - compiled-in modular plugins that hook into compositor events and can manipulate compositor state through a controlled API.

## Plugin Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Plugins                               │
│  Example │ Blur │ Scale │ Wallpaper │ Notifications    │
│  Layouts                                                 │
├─────────────────────────────────────────────────────────┤
│              CompositorAPI                               │
│  - Controlled access to compositor state                │
│  - View manipulation, workspace ops                     │
│  - Safe abstraction layer                               │
├─────────────────────────────────────────────────────────┤
│              PluginManager                               │
│  - Owns plugins (std::unique_ptr<Plugin>)               │
│  - Dispatches events (frame, view, key)                 │
│  - Implements CompositorAPI                             │
├─────────────────────────────────────────────────────────┤
│                    Server                                │
│  - Creates PluginManager                                 │
│  - Registers built-in plugins                            │
│  - Dispatches events from wlroots                        │
└─────────────────────────────────────────────────────────┘
```

## Creating a Plugin

### 1. Create Plugin Class

```cpp
#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>

namespace havel {

class MyPlugin : public Plugin {
public:
    const char* name() const override { return "my_plugin"; }
    const char* version() const override { return "0.1.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        // Plugin initialization
    }
    
    void fini() override {
        // Plugin cleanup
        m_api = nullptr;
    }
    
    // Optional: Event hooks
    void onOutputFrame(const OutputFrameEvent& event) override { }
    void onViewMap(const ViewEvent& event) override { }
    void onViewUnmap(const ViewEvent& event) override { }
    void onViewDestroy(const ViewEvent& event) override { }
    bool onKey(const KeyEvent& event) override { return false; }
    void renderOverlay(void* renderPass) override { }
    
private:
    CompositorAPI* m_api = nullptr;
};

// Plugin factory function
Plugin* create_my_plugin() {
    return new MyPlugin();
}

} // namespace havel
```

### 2. Register Plugin

In `src/wm/core/Server.cpp`:

```cpp
#include <wm/plugins/Plugins.hpp>

Server::Server() {
    // ... initialize workspaces ...
    
    m_pluginManager.initialize(this);
    
    // Register your plugin
    registerPlugin(std::unique_ptr<Plugin>(create_my_plugin()));
}
```

### 3. Add to Build

In `CMakeLists.txt`:

```cmake
set(WM_CORE_SOURCES
    # ... other sources ...
    src/wm/plugins/MyPlugin.cpp
)
```

## Plugin Hooks

### Lifecycle Hooks

| Hook | Called When | Purpose |
|------|-------------|---------|
| `init()` | Plugin registered | Initialize plugin state |
| `fini()` | Compositor shutdown | Cleanup resources |
| `loadConfig()` | Config loaded | Load plugin settings |

### Event Hooks

| Hook | Event | Return |
|------|-------|--------|
| `onOutputFrame()` | Every frame per output | void |
| `onViewMap()` | Window opened | void |
| `onViewUnmap()` | Window hidden | void |
| `onViewDestroy()` | Window closed | void |
| `onKey()` | Key pressed | bool (true = consumed) |
| `renderOverlay()` | Render pass | void |

## CompositorAPI

Plugins interact with the compositor through `CompositorAPI`:

### View Operations

```cpp
View* getFocusedView();
void focusView(View* view);
void closeView(View* view);
void setViewPosition(View* view, int x, int y);
void setViewOpacity(View* view, float alpha);
```

### Workspace Operations

```cpp
uint32_t getActiveWorkspace();
void setActiveWorkspace(uint32_t id);
uint32_t getWorkspaceCount();
```

### Background/Output

```cpp
void setBackgroundColor(float r, float g, float b);
void scheduleRedraw();
int getOutputWidth();
int getOutputHeight();
```

## Built-in Plugins

### Example Plugin
- **File:** `ExamplePlugin.cpp`
- **Keybinding:** Meta+X
- **Action:** Switch to next workspace

### Blur Plugin
- **File:** `BlurPlugin.cpp`
- **Keybinding:** Meta+B
- **Action:** Toggle blur effect (stub)

### Scale Plugin
- **File:** `ScalePlugin.cpp`
- **Keybinding:** Meta+S
- **Action:** Toggle overview scale (stub)

### Wallpaper Plugin
- **File:** `WallpaperPlugin.cpp`
- **Keybinding:** Meta+W
- **Action:** Cycle background colors

### Notifications Plugin
- **File:** `NotificationsPlugin.cpp`
- **Keybinding:** Meta+N
- **Action:** Show test notification

### Custom Layouts Plugin
- **File:** `CustomLayoutsPlugin.cpp`
- **Keybindings:** Meta+H/V/G/T
- **Action:** Switch tiling layouts

## Event Flow

### Frame Event
```
wlr_bridge.c:output_frame()
    → havel_cpp_dispatch_output_frame()
        → PluginManager::dispatchOutputFrame()
            → plugin->onOutputFrame()  // For each plugin
    → wlr_scene_output_commit()
```

### Key Event
```
Server::handleKey()
    → PluginManager::dispatchKey()
        → plugin->onKey()  // For each plugin
            → If returns true: event consumed, stop
    → If not consumed: compositor handles
```

### View Events
```
Server::onViewMapped/Unmapped/Destroyed()
    → PluginManager::dispatchView*()
        → plugin->onView*()  // For each plugin
```

## Plugin Development Guidelines

1. **Keep plugins modular** - Each plugin should do one thing well
2. **Use CompositorAPI** - Don't access Server internals directly
3. **Return true from onKey()** only if you consume the event
4. **Clean up in fini()** - Release any resources
5. **Log with printf** - Use `[PluginName]` prefix for debugging

## Future: Level 3 Plugins (Runtime Loading)

Currently plugins are Level 2 (compiled-in). Future support for Level 3 (runtime `.so` loading) would require:

- Stable ABI definition
- Plugin versioning
- `dlopen()` / `dlsym()` integration
- Plugin config file format

## Debugging Plugins

Enable debug logging to see plugin activity:

```
[ExamplePlugin] Initialized
[ExamplePlugin] Meta+X consumed by plugin!
[WallpaperPlugin] Wallpaper color: (0.10, 0.10, 0.15)
```
