#pragma once

#include <cstdint>

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
    
    // Workspace operations
    virtual uint32_t getActiveWorkspace() = 0;
    virtual void setActiveWorkspace(uint32_t id) = 0;
    virtual uint32_t getWorkspaceCount() = 0;
    
    // View transforms (for scale, overview, etc.)
    virtual void setViewPosition(View* view, int x, int y) = 0;
    virtual void setViewOpacity(View* view, float alpha) = 0;
    
    // Output/background control
    virtual void setBackgroundColor(float r, float g, float b) = 0;
    // Note: Scale requires scene graph extension (future)
    
    // Rendering
    virtual void scheduleRedraw() = 0;
    
    // Output info
    virtual int getOutputWidth() = 0;
    virtual int getOutputHeight() = 0;
    
    // Keybinding registration (future)
    // virtual void registerKeybinding(uint32_t mods, uint32_t key, Callback cb) = 0;
};

} // namespace havel
