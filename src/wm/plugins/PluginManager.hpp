#pragma once

#include "Plugin.hpp"
#include "CompositorAPI.hpp"
#include "PluginConfig.hpp"
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

    // Overlay rendering
    void renderOverlays(void* renderer);

    // Mouse events
    void onMouseMotion(int x, int y);
    void onMouseButton(uint32_t button, bool pressed, int x, int y);

    // CompositorAPI implementation
    View* getFocusedView() override;
    void focusView(View* view) override;
    void closeView(View* view) override;

    // Window enumeration
    std::vector<View*> getAllViews() override;
    std::vector<View*> getViewsInWorkspace(uint32_t workspaceId) override;
    View* getViewById(uint64_t id) override;
    void focusViewById(uint64_t id) override;

    // Window metadata
    std::string getViewAppId(View* view) override;
    std::string getViewTitle(View* view) override;

    // Window texture for thumbnails
    uint32_t getViewTextureId(View* view) override;
    int getViewTextureWidth(View* view) override;
    int getViewTextureHeight(View* view) override;

    uint32_t getActiveWorkspace() override;
    void setActiveWorkspace(uint32_t id) override;
    uint32_t getWorkspaceCount() override;

    void setViewPosition(View* view, int x, int y) override;
    void setViewOpacity(View* view, float alpha) override;
    void setViewGeometry(View* view, int x, int y, int w, int h) override;
    void setBackgroundColor(float r, float g, float b) override;
    void setGamma(float gamma) override;
    void setTemperature(int kelvin) override;
    void setBrightness(float brightness) override;

    void scheduleRedraw() override;

    int getOutputWidth() override;
    int getOutputHeight() override;

    // Cursor position (for HotCorners, etc.)
    double getCursorX() override;
    double getCursorY() override;

    // Get list of loaded plugins
    const std::vector<std::unique_ptr<Plugin>>& plugins() const { return m_plugins; }

    // Configuration
    bool loadConfig(const std::string& path);
    bool isPluginEnabled(const std::string& name) const;
    bool reloadConfig();  // Hot-reload configuration

private:
    std::vector<std::unique_ptr<Plugin>> m_plugins;
    void* m_server = nullptr;  // Server* - opaque to plugins
    bool m_initialized = false;
    PluginConfig m_config;
    std::string m_configPath;  // Store config path for hot-reload
};

} // namespace havel
