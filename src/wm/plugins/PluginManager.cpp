// Plugin Manager Implementation - Enhanced Architecture

#include "PluginManager.hpp"
#include <wm/Server.hpp>
#include <Logger.h>
#include <algorithm>
#include <cstdio>

namespace havel {

PluginManager::PluginManager() = default;

PluginManager::~PluginManager() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

void PluginManager::initialize(void* server) {
    if (m_initialized) {
        LOG_WARN("[PluginManager] Already initialized");
        return;
    }
    
    m_server = server;
    m_initialized = true;
    
    LOG_INFO("[PluginManager] Initialized with %zu plugins", m_plugins.size());
    
    // Initialize all plugins in priority order
    sortPluginsByPriority();
    
    for (auto& plugin : m_plugins) {
        // Call preInit hook
        plugin->preInit();
        
        // Initialize plugin
        plugin->init(this);
        
        // Call postInit hook
        plugin->postInit();
        
        // Emit plugin loaded event
        PluginEvent event;
        event.type = PluginEventType::PluginLoaded;
        event.data = plugin->getName();
        emitEvent(event);
        
        LOG_INFO("[PluginManager] Plugin '%s' v%s initialized", 
                 plugin->getName().c_str(), plugin->getVersion().c_str());
    }
}

void PluginManager::shutdown() {
    if (!m_initialized) return;
    
    LOG_INFO("[PluginManager] Shutting down %zu plugins", m_plugins.size());
    
    // Shutdown all plugins in reverse priority order
    for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it) {
        auto& plugin = *it;
        
        // Call preShutdown hook
        plugin->preShutdown();
        
        // Emit plugin unloaded event
        PluginEvent event;
        event.type = PluginEventType::PluginUnloaded;
        event.data = plugin->getName();
        emitEvent(event);
        
        // Shutdown plugin
        plugin->fini();
        
        // Call postShutdown hook
        plugin->postShutdown();
        
        LOG_INFO("[PluginManager] Plugin '%s' shut down", plugin->getName().c_str());
    }
    
    m_plugins.clear();
    m_pluginSettings.clear();
    clearEventListeners();
    
    m_server = nullptr;
    m_initialized = false;
}

// ============================================================================
// Plugin Management
// ============================================================================

void PluginManager::registerPlugin(std::unique_ptr<Plugin> plugin) {
    if (!plugin) {
        LOG_ERROR("[PluginManager] Attempted to register null plugin");
        return;
    }
    
    // Check for duplicate
    if (hasPlugin(plugin->getName())) {
        LOG_ERROR("[PluginManager] Plugin '%s' already registered", 
                  plugin->getName().c_str());
        return;
    }
    
    LOG_INFO("[PluginManager] Registering plugin '%s' v%s (priority=%d)",
             plugin->getName().c_str(), 
             plugin->getVersion().c_str(),
             plugin->getInfo().priority);
    
    m_plugins.push_back(std::move(plugin));
    sortPluginsByPriority();
}

void PluginManager::unregisterPlugin(const std::string& name) {
    auto it = std::find_if(m_plugins.begin(), m_plugins.end(),
        [&name](const std::unique_ptr<Plugin>& p) {
            return p->getName() == name;
        });
    
    if (it != m_plugins.end()) {
        // Shutdown plugin first
        (*it)->fini();
        
        // Emit event
        PluginEvent event;
        event.type = PluginEventType::PluginUnloaded;
        event.data = name;
        emitEvent(event);
        
        LOG_INFO("[PluginManager] Unregistered plugin '%s'", name.c_str());
        m_plugins.erase(it);
        m_pluginSettings.erase(name);
    }
}

Plugin* PluginManager::getPlugin(const std::string& name) {
    auto it = std::find_if(m_plugins.begin(), m_plugins.end(),
        [&name](const std::unique_ptr<Plugin>& p) {
            return p->getName() == name;
        });
    
    return (it != m_plugins.end()) ? it->get() : nullptr;
}

const Plugin* PluginManager::getPlugin(const std::string& name) const {
    auto it = std::find_if(m_plugins.begin(), m_plugins.end(),
        [&name](const std::unique_ptr<Plugin>& p) {
            return p->getName() == name;
        });
    
    return (it != m_plugins.end()) ? it->get() : nullptr;
}

bool PluginManager::hasPlugin(const std::string& name) const {
    return std::any_of(m_plugins.begin(), m_plugins.end(),
        [&name](const std::unique_ptr<Plugin>& p) {
            return p->getName() == name;
        });
}

void PluginManager::sortPluginsByPriority() {
    std::sort(m_plugins.begin(), m_plugins.end(),
        [](const std::unique_ptr<Plugin>& a, const std::unique_ptr<Plugin>& b) {
            return a->getInfo().priority < b->getInfo().priority;
        });
}

void PluginManager::loadPluginSettings(Plugin& plugin) {
    const std::string& name = plugin.getName();
    
    // Create settings object
    auto& settings = m_pluginSettings[name];
    
    // Load from config using PluginConfig
    std::unordered_map<std::string, std::string> configData;
    
    // Get all settings for this plugin from PluginConfig
    // PluginConfig stores settings as plugin.key = value
    // We need to extract all keys for this plugin
    auto& globalConfig = PluginConfig::getInstance();
    
    // Common settings that plugins might use
    const char* commonKeys[] = {
        "enabled", "keybinding",
        "scaleFactor", "gridSpacing", "thumbnailWidth", "thumbnailHeight", "maxVisibleWindows", "highlightColor",
        "backgroundColor", "borderColor", "textColor", "accentColor",
        "barHeight", "foregroundColor",
        NULL
    };
    
    for (int i = 0; commonKeys[i] != NULL; i++) {
        std::string value = globalConfig.getValue(name, commonKeys[i], "");
        if (!value.empty()) {
            configData[commonKeys[i]] = value;
        }
    }
    
    // Load settings into plugin
    settings.loadFromMap(configData);
    
    // Notify plugin that settings are loaded
    plugin.onSettingsLoaded();
    
    if (!configData.empty()) {
        LOG_INFO("[PluginManager] Loaded %zu settings for plugin '%s'", configData.size(), name.c_str());
    }
}

PluginSettings& PluginManager::getPluginSettings(const std::string& name) {
    return m_pluginSettings[name];
}

// ============================================================================
// Event System
// ============================================================================

bool PluginManager::emitEvent(PluginEvent& event) {
    // Set timestamp
    event.timestamp = 0;  // Would use actual time
    
    // Call event listeners first
    bool consumed = false;
    for (const auto& listener : m_eventListeners) {
        if (listener.type == event.type || listener.type == PluginEventType::Custom) {
            listener.callback(event);
        }
    }
    
    // Then call plugin handlers
    for (auto& plugin : m_plugins) {
        if (plugin->onEvent(event)) {
            consumed = true;
            break;  // First plugin to consume wins
        }
    }
    
    return consumed;
}

PluginManager::EventListenerId PluginManager::addEventListener(
    PluginEventType type, EventListener callback) {
    
    EventListenerId id = m_nextListenerId++;
    m_eventListeners.push_back({id, type, std::move(callback)});
    return id;
}

void PluginManager::removeEventListener(EventListenerId id) {
    m_eventListeners.erase(
        std::remove_if(m_eventListeners.begin(), m_eventListeners.end(),
            [id](const EventListenerEntry& e) { return e.id == id; }),
        m_eventListeners.end());
}

void PluginManager::clearEventListeners() {
    m_eventListeners.clear();
}

// ============================================================================
// Event Dispatch
// ============================================================================

void PluginManager::dispatchOutputFrame(const OutputFrameEvent& event) {
    for (auto& plugin : m_plugins) {
        plugin->onOutputFrame(event);
    }
}

void PluginManager::dispatchViewMap(const ViewEvent& event) {
    // Emit event
    PluginEvent pluginEvent;
    pluginEvent.type = PluginEventType::WindowMapped;
    pluginEvent.data = event.view;
    emitEvent(pluginEvent);
    
    // Call plugin handlers
    for (auto& plugin : m_plugins) {
        plugin->onViewMap(event);
    }
}

void PluginManager::dispatchViewUnmap(const ViewEvent& event) {
    PluginEvent pluginEvent;
    pluginEvent.type = PluginEventType::WindowUnmapped;
    pluginEvent.data = event.view;
    emitEvent(pluginEvent);
    
    for (auto& plugin : m_plugins) {
        plugin->onViewUnmap(event);
    }
}

void PluginManager::dispatchViewDestroy(const ViewEvent& event) {
    PluginEvent pluginEvent;
    pluginEvent.type = PluginEventType::WindowDestroyed;
    pluginEvent.data = event.view;
    emitEvent(pluginEvent);
    
    for (auto& plugin : m_plugins) {
        plugin->onViewDestroy(event);
    }
}

bool PluginManager::dispatchKey(const KeyEvent& event) {
    // Emit event
    PluginEvent pluginEvent;
    pluginEvent.type = PluginEventType::Custom;
    pluginEvent.data = &const_cast<KeyEvent&>(event);
    
    // Check if any plugin consumes the key event
    for (auto& plugin : m_plugins) {
        if (plugin->onKey(event)) {
            return true;  // Event consumed
        }
    }
    
    return false;  // Event not consumed, pass to compositor
}

// ============================================================================
// Overlay Rendering
// ============================================================================

void PluginManager::renderOverlays(void* renderer) {
    for (auto& plugin : m_plugins) {
        plugin->renderOverlay(renderer);
    }
}

// ============================================================================
// Input Handling
// ============================================================================

void PluginManager::onMouseMotion(int x, int y) {
    for (auto& plugin : m_plugins) {
        plugin->onMouseMotion(x, y);
    }
}

void PluginManager::onMouseButton(uint32_t button, bool pressed, int x, int y) {
    for (auto& plugin : m_plugins) {
        plugin->onMouseButton(button, pressed, x, y);
    }
}

// ============================================================================
// Configuration
// ============================================================================

bool PluginManager::loadConfig(const std::string& path) {
    m_configPath = path;
    return m_config.load(path);
}

bool PluginManager::reloadConfig() {
    if (m_configPath.empty()) {
        LOG_ERROR("[PluginManager] No config path stored");
        return false;
    }
    
    LOG_INFO("[PluginManager] Reloading configuration from %s", m_configPath.c_str());
    
    // Store enabled state
    std::vector<std::string> enabledPlugins;
    for (const auto& plugin : m_plugins) {
        if (m_config.isEnabled(plugin->getName())) {
            enabledPlugins.push_back(plugin->getName());
        }
    }
    
    // Reload config
    if (!m_config.load(m_configPath)) {
        LOG_ERROR("[PluginManager] Failed to reload config");
        return false;
    }
    
    // Reload settings for each plugin
    for (auto& plugin : m_plugins) {
        loadPluginSettings(*plugin);
    }
    
    LOG_INFO("[PluginManager] Configuration reloaded");
    return true;
}

bool PluginManager::isPluginEnabled(const std::string& name) const {
    return m_config.isEnabled(name);
}

// ============================================================================
// CompositorAPI Implementation
// (Delegates to Server - unchanged from before)
// ============================================================================

View* PluginManager::getFocusedView() {
    if (!m_server) return nullptr;
    auto* server = static_cast<Server*>(m_server);
    return server->getFocusedView();
}

void PluginManager::focusView(View* view) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    server->focusView(view);
}

void PluginManager::closeView(View* view) {
    if (!m_server || !view) return;
    auto* server = static_cast<Server*>(m_server);
    // closeView not implemented;
}

std::vector<View*> PluginManager::getAllViews() {
    if (!m_server) return {};
    auto* server = static_cast<Server*>(m_server);
    return server->getAllViews();
}

std::vector<View*> PluginManager::getViewsInWorkspace(uint32_t workspaceId) {
    if (!m_server) return {};
    auto* server = static_cast<Server*>(m_server);
    return server->getViewsInWorkspace(workspaceId);
}

View* PluginManager::getViewById(uint64_t id) {
    if (!m_server) return nullptr;
    auto* server = static_cast<Server*>(m_server);
    return server->getViewById(id);
}

void PluginManager::focusViewById(uint64_t id) {
    View* view = getViewById(id);
    if (view) focusView(view);
}

std::string PluginManager::getViewAppId(View* view) {
    if (!m_server || !view) return "";
    auto* server = static_cast<Server*>(m_server);
    return server->getViewAppId(view);
}

std::string PluginManager::getViewTitle(View* view) {
    if (!m_server || !view) return "";
    auto* server = static_cast<Server*>(m_server);
    return server->getViewTitle(view);
}

int PluginManager::getViewX(View* view) {
    if (!m_server || !view) return 0;
    auto* server = static_cast<Server*>(m_server);
    return view->geom().x;
}

int PluginManager::getViewY(View* view) {
    if (!m_server || !view) return 0;
    auto* server = static_cast<Server*>(m_server);
    return view->geom().y;
}

int PluginManager::getViewWidth(View* view) {
    if (!m_server || !view) return 0;
    auto* server = static_cast<Server*>(m_server);
    return view->geom().w;
}

int PluginManager::getViewHeight(View* view) {
    if (!m_server || !view) return 0;
    auto* server = static_cast<Server*>(m_server);
    return view->geom().h;
}

bool PluginManager::isViewFloating(View* view) {
    if (!m_server || !view) return false;
    auto* server = static_cast<Server*>(m_server);
    return view->isFloating();
}

uint32_t PluginManager::getViewTextureId(View* view) {
    if (!m_server || !view) return 0;
    return view->textureId();
}

int PluginManager::getViewTextureWidth(View* view) {
    if (!m_server || !view) return 0;
    return view->textureWidth();
}

int PluginManager::getViewTextureHeight(View* view) {
    if (!m_server || !view) return 0;
    return view->textureHeight();
}

uint32_t PluginManager::getActiveWorkspace() {
    if (!m_server) return 0;
    auto* server = static_cast<Server*>(m_server);
    return server->activeWorkspace() ? server->activeWorkspace()->id() : 0;
}

void PluginManager::setActiveWorkspace(uint32_t id) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    server->setActiveWorkspace(id);
}

uint32_t PluginManager::getWorkspaceCount() {
    return WORKSPACE_COUNT;
}

void PluginManager::setViewPosition(View* view, int x, int y) {
    if (!m_server || !view) return;
    auto* server = static_cast<Server*>(m_server);
    server->setViewPosition(view, x, y);
}

void PluginManager::setViewOpacity(View* view, float alpha) {
    if (!m_server || !view) return;
    auto* server = static_cast<Server*>(m_server);
    server->setViewOpacity(view, alpha);
}

void PluginManager::setViewGeometry(View* view, int x, int y, int w, int h) {
    if (!m_server || !view) return;
    auto* server = static_cast<Server*>(m_server);
    server->setViewGeometry(view, x, y, w, h);
}

void PluginManager::setBackgroundColor(float r, float g, float b) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    server->setBackgroundColor(r, g, b);
}

void PluginManager::setGamma(float gamma) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    server->setGamma(gamma);
}

void PluginManager::setTemperature(int kelvin) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    server->setTemperature(kelvin);
}

void PluginManager::setBrightness(float brightness) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    server->setBrightness(brightness);
}

void PluginManager::setGammaForOutput(int output_index, float gamma) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    server->setGamma(gamma);
}

void PluginManager::setTemperatureForOutput(int output_index, int kelvin) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    server->setTemperature(kelvin);
}

void PluginManager::setBrightnessForOutput(int output_index, float brightness) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    server->setBrightness(brightness);
}

void PluginManager::setZoomForOutput(int output_index, float zoom) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    // setZoom not implemented
}

void PluginManager::scheduleRedraw() {
    // Handled by frame loop in C layer
}

void* PluginManager::getOverlayRenderer() {
    if (!m_server) return nullptr;
    auto* server = static_cast<Server*>(m_server);
    return server->getOverlayRenderer();
}

int PluginManager::getOutputWidth() {
    if (!m_server) return 1920;
    auto* server = static_cast<Server*>(m_server);
    return server->getOutputWidth();
}

int PluginManager::getOutputHeight() {
    if (!m_server) return 1080;
    auto* server = static_cast<Server*>(m_server);
    return server->getOutputHeight();
}

double PluginManager::getCursorX() {
    if (!m_server) return 0.0;
    auto* server = static_cast<Server*>(m_server);
    return server->cursorX();
}

double PluginManager::getCursorY() {
    if (!m_server) return 0.0;
    auto* server = static_cast<Server*>(m_server);
    return server->cursorY();
}

void* PluginManager::getNativeHandle() {
    if (!m_server) return nullptr;
    auto* server = static_cast<Server*>(m_server);
    return server->nativeHandle();
}

} // namespace havel
