#pragma once

#include <cstdint>
#include <vector>

namespace havel {

// Forward declarations - plugins only see opaque pointers
class View;
class Workspace;
class Server;

/**
 * Controlled API surface for plugins.
 * 
 * Plugins receive a pointer to this interface in init().
 * They use it to interact with the compositor safely.
 * 
 * This prevents plugins from depending on internal implementation.
 */
class CompositorAPI {
public:
    virtual ~CompositorAPI() = default;
    
    // View manipulation
    virtual View* getFocusedView() = 0;
    virtual void focusView(View* view) = 0;
    virtual void closeView(View* view) = 0;
    
    // Window enumeration (for Alt-Tab, Overview, etc.)
    virtual std::vector<View*> getAllViews() = 0;
    virtual std::vector<View*> getViewsInWorkspace(uint32_t workspaceId) = 0;
    virtual View* getViewById(uint64_t id) = 0;
    virtual void focusViewById(uint64_t id) = 0;

    // Window metadata (from XDG surface)
    virtual std::string getViewAppId(View* view) = 0;
    virtual std::string getViewTitle(View* view) = 0;

    // Workspace operations
    virtual uint32_t getActiveWorkspace() = 0;
    virtual void setActiveWorkspace(uint32_t id) = 0;
    virtual uint32_t getWorkspaceCount() = 0;
    
    // View transforms (for scale, overview, etc.)
    virtual void setViewPosition(View* view, int x, int y) = 0;
    virtual void setViewOpacity(View* view, float alpha) = 0;
    virtual void setViewGeometry(View* view, int x, int y, int w, int h) = 0;
    
    // Output/background control
    virtual void setBackgroundColor(float r, float g, float b) = 0;
    
    // Gamma/temperature control (per-output)
    virtual void setGamma(float gamma) = 0;  // 0.1 - 2.0
    virtual void setTemperature(int kelvin) = 0;  // 3000K - 6500K
    virtual void setBrightness(float brightness) = 0;  // 0.1 - 1.0
    // Note: Scale requires scene graph extension (future)
    
    // Rendering
    virtual void scheduleRedraw() = 0;
    
    // Output info
    virtual int getOutputWidth() = 0;
    virtual int getOutputHeight() = 0;
    
    // Cursor position (for HotCorners, etc.)
    virtual double getCursorX() = 0;
    virtual double getCursorY() = 0;
    
    // Keybinding registration (future)
    // virtual void registerKeybinding(uint32_t mods, uint32_t key, Callback cb) = 0;
};

} // namespace havel
