#pragma once

#include <string>
#include <cstdint>
#include <memory>

namespace havel {

// Forward declarations
class CompositorAPI;
struct OutputFrameEvent;
struct ViewEvent;
struct KeyEvent;

/**
 * Plugin interface - all plugins must implement this
 * 
 * Plugins are Level 2: internal modules, compiled-in but modular.
 * They hook into compositor lifecycle via callbacks.
 */
class Plugin {
public:
    virtual ~Plugin() = default;
    
    // Plugin metadata
    virtual const char* name() const = 0;
    virtual const char* version() const = 0;
    
    // Lifecycle
    virtual void init(CompositorAPI* api) = 0;
    virtual void fini() = 0;
    
    // Event hooks (override as needed)
    virtual void onOutputFrame(const OutputFrameEvent& event) { (void)event; }
    virtual void onViewMap(const ViewEvent& event) { (void)event; }
    virtual void onViewUnmap(const ViewEvent& event) { (void)event; }
    virtual void onViewDestroy(const ViewEvent& event) { (void)event; }
    virtual bool onKey(const KeyEvent& event) { (void)event; return false; }
    
    // Render hooks
    virtual void renderOverlay(void* renderPass) { (void)renderPass; }
    
    // Configuration
    virtual void loadConfig(const std::string& configPath) { (void)configPath; }
};

/**
 * Event data structures
 */
struct OutputFrameEvent {
    void* output;        // wlr_output*
    void* sceneOutput;   // wlr_scene_output*
    int width;
    int height;
    float refresh;       // mHz
};

struct ViewEvent {
    void* view;          // View*
    const char* appId;
    const char* title;
    uint32_t workspace;
    int x, y, width, height;
};

struct KeyEvent {
    uint32_t keycode;
    uint32_t modifiers;
    bool pressed;
};

/**
 * Plugin factory function type
 * Plugins export this function: extern "C" Plugin* create_plugin();
 */
using PluginCreateFn = Plugin* (*)();
using PluginDestroyFn = void (*)(Plugin*);

} // namespace havel
