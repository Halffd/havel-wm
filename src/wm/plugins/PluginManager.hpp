#pragma once

#include "Plugin.hpp"
#include "CompositorAPI.hpp"
#include <vector>
#include <memory>
#include <string>

namespace havel {

/**
 * Plugin manager - owns plugins and dispatches events
 * 
 * Level 2 plugin system: internal modules, compiled-in.
 * Future: Level 3 with dlopen() for runtime loading.
 */
class PluginManager : public CompositorAPI {
public:
    PluginManager();
    ~PluginManager();
    
    // Initialize with compositor server pointer
    void initialize(void* server);
    void shutdown();
    
    // Plugin management
    void registerPlugin(std::unique_ptr<Plugin> plugin);
    void unregisterPlugin(const char* name);
    
    // Event dispatch
    void dispatchOutputFrame(const OutputFrameEvent& event);
    void dispatchViewMap(const ViewEvent& event);
    void dispatchViewUnmap(const ViewEvent& event);
    void dispatchViewDestroy(const ViewEvent& event);
    bool dispatchKey(const KeyEvent& event);  // Returns true if consumed
    void dispatchRenderOverlay(void* renderPass);
    
    // CompositorAPI implementation
    View* getFocusedView() override;
    void focusView(View* view) override;
    void closeView(View* view) override;
    
    uint32_t getActiveWorkspace() override;
    void setActiveWorkspace(uint32_t id) override;
    uint32_t getWorkspaceCount() override;
    
    void setViewPosition(View* view, int x, int y) override;
    void setViewOpacity(View* view, float alpha) override;
    void setBackgroundColor(float r, float g, float b) override;
    void setGamma(float gamma) override;
    void setTemperature(int kelvin) override;
    void setBrightness(float brightness) override;

    void scheduleRedraw() override;
    
    int getOutputWidth() override;
    int getOutputHeight() override;
    
    // Get list of loaded plugins
    const std::vector<std::unique_ptr<Plugin>>& plugins() const { return m_plugins; }
    
private:
    std::vector<std::unique_ptr<Plugin>> m_plugins;
    void* m_server = nullptr;  // Server* - opaque to plugins
    bool m_initialized = false;
};

} // namespace havel
