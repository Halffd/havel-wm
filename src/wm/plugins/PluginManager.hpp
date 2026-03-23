#pragma once

#include "Plugin.hpp"
#include "CompositorAPI.hpp"
#include "PluginConfig.hpp"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

namespace havel {

/**
 * Plugin Manager - Enhanced plugin lifecycle and event system
 *
 * Features:
 * - Plugin lifecycle management (init/fini with hooks)
 * - Type-safe plugin settings
 * - Event system for plugin communication
 * - Per-plugin logging
 * - Plugin priority ordering
 * - Configuration hot-reload
 *
 * Level 2: Compiled-in plugins
 * Future: Level 3 with dlopen() for runtime loading
 */
class PluginManager : public CompositorAPI {
public:
    PluginManager();
    ~PluginManager();

    // -------------------------------------------------------------------------
    // Initialization
    // -------------------------------------------------------------------------
    
    void initialize(void* server);
    void shutdown();
    
    bool isInitialized() const { return m_initialized; }

    // -------------------------------------------------------------------------
    // Plugin Management
    // -------------------------------------------------------------------------
    
    /**
     * Register a plugin
     * Plugins are sorted by priority after registration
     */
    void registerPlugin(std::unique_ptr<Plugin> plugin);
    
    /**
     * Unregister a plugin by name
     */
    void unregisterPlugin(const std::string& name);
    
    /**
     * Get plugin by name
     */
    Plugin* getPlugin(const std::string& name);
    const Plugin* getPlugin(const std::string& name) const;
    
    /**
     * Check if plugin is loaded
     */
    bool hasPlugin(const std::string& name) const;
    
    /**
     * Get list of loaded plugins
     */
    const std::vector<std::unique_ptr<Plugin>>& plugins() const { return m_plugins; }
    
    /**
     * Get plugin count
     */
    size_t pluginCount() const { return m_plugins.size(); }

    // -------------------------------------------------------------------------
    // Event System
    // -------------------------------------------------------------------------
    
    /**
     * Emit event to all plugins
     * Returns true if any plugin consumed the event
     */
    bool emitEvent(PluginEvent& event);
    
    /**
     * Add event listener
     */
    using EventListenerId = size_t;
    EventListenerId addEventListener(PluginEventType type, EventListener callback);
    
    /**
     * Remove event listener
     */
    void removeEventListener(EventListenerId id);
    
    /**
     * Clear all event listeners
     */
    void clearEventListeners();

    // -------------------------------------------------------------------------
    // Event Dispatch (called from compositor)
    // -------------------------------------------------------------------------
    
    void dispatchOutputFrame(const OutputFrameEvent& event);
    void dispatchViewMap(const ViewEvent& event);
    void dispatchViewUnmap(const ViewEvent& event);
    void dispatchViewDestroy(const ViewEvent& event);
    bool dispatchKey(const KeyEvent& event);  // Returns true if consumed

    // -------------------------------------------------------------------------
    // Overlay Rendering
    // -------------------------------------------------------------------------
    
    void renderOverlays(void* renderer);

    // -------------------------------------------------------------------------
    // Input Handling
    // -------------------------------------------------------------------------
    
    void onMouseMotion(int x, int y);
    void onMouseButton(uint32_t button, bool pressed, int x, int y);

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------
    
    /**
     * Load configuration from file
     */
    bool loadConfig(const std::string& path);
    
    /**
     * Reload configuration (hot-reload)
     */
    bool reloadConfig();
    
    /**
     * Check if plugin is enabled in config
     */
    bool isPluginEnabled(const std::string& name) const;
    
    /**
     * Get plugin settings
     */
    PluginSettings& getPluginSettings(const std::string& name);

    // -------------------------------------------------------------------------
    // CompositorAPI Implementation
    // -------------------------------------------------------------------------
    
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

    // Window geometry
    int getViewX(View* view) override;
    int getViewY(View* view) override;
    int getViewWidth(View* view) override;
    int getViewHeight(View* view) override;
    bool isViewFloating(View* view) override;

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

    // Per-monitor control
    void setGammaForOutput(int output_index, float gamma) override;
    void setTemperatureForOutput(int output_index, int kelvin) override;
    void setBrightnessForOutput(int output_index, float brightness) override;
    void setZoomForOutput(int output_index, float zoom) override;

    void scheduleRedraw() override;
    void* getOverlayRenderer() override;

    int getOutputWidth() override;
    int getOutputHeight() override;

    // Cursor position
    double getCursorX() override;
    double getCursorY() override;

    // Native handle
    void* getNativeHandle() override;

private:
    // Sort plugins by priority
    void sortPluginsByPriority();
    
    // Load plugin settings from config
    void loadPluginSettings(Plugin& plugin);
    
    // Event listener management
    struct EventListenerEntry {
        EventListenerId id;
        PluginEventType type;
        EventListener callback;
    };
    
    std::vector<EventListenerEntry> m_eventListeners;
    EventListenerId m_nextListenerId = 1;

    // Plugins
    std::vector<std::unique_ptr<Plugin>> m_plugins;
    
    // Plugin settings cache
    std::unordered_map<std::string, PluginSettings> m_pluginSettings;
    
    // Server pointer
    void* m_server = nullptr;
    bool m_initialized = false;
    
    // Configuration
    PluginConfig m_config;
    std::string m_configPath;
};

} // namespace havel
