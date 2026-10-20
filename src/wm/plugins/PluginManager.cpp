// Plugin Manager Implementation - Enhanced Architecture

#include <chrono>
#include "PluginManager.hpp"
#include <wm/Server.hpp>
#include <wm/bridge.h>
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

        // Load plugin config before postInit so hooks see real settings.
        // Previously config was only loaded on reload_config IPC — first
        // boot ran every plugin on defaults.
        if (!m_configPath.empty()) {
            loadPluginSettings(*plugin);
        }

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

    // Full lifecycle at registration time when the manager is already
    // initialized. The Server registers built-in plugins AFTER
    // PluginManager::initialize - previously that meant they never
    // received init(this) at all (first real client connect crashed
    // ServerDecorationPlugin onViewMap on its null m_api; every plugin
    // has been running with a null CompositorAPI since forever).
    if (m_initialized) {
        plugin->preInit();
        plugin->init(this);
        if (!m_configPath.empty()) {
            loadPluginSettings(*plugin);
        }
        plugin->postInit();

        PluginEvent event;
        event.type = PluginEventType::PluginLoaded;
        event.data = plugin->getName();
        emitEvent(event);

        LOG_INFO("[PluginManager] Plugin '%s' v%s initialized",
                 plugin->getName().c_str(), plugin->getVersion().c_str());
    } else {
        // Pre-initialization registration (not expected today):
        // config only, lifecycle happens in the initialize loop
        if (!m_configPath.empty()) {
            loadPluginSettings(*plugin);
        }
    }

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

    // Let the plugin pull its own keys straight from the global config
    // (PluginConfig::getInstance().get*Value(name, key, default)), the
    // pattern ScalePlugin/AltTabPlugin already use. Previously this
    // extracted a hardcoded 15-key whitelist into PluginSettings that
    // nothing consumed — plugins with other keys silently got defaults.
    plugin.loadConfig(m_configPath);

    // Notify plugin that settings are loaded
    plugin.onSettingsLoaded();
}

PluginSettings& PluginManager::getPluginSettings(const std::string& name) {
    return m_pluginSettings[name];
}

// ============================================================================
// Event System
// ============================================================================

bool PluginManager::emitEvent(PluginEvent& event) {
    // Set timestamp
    event.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
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
            LOG_INFO("[PluginManager] Key consumed by plugin '%s' "
                     "(keysym=0x%x mods=0x%x pressed=%d)",
                     plugin->getName().c_str(), event.keysym,
                     event.modifiers, event.pressed);
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

bool PluginManager::pluginCommand(const std::string& pluginName, const std::string& cmd,
                                  const std::string& arg) {
    for (auto& plugin : m_plugins) {
        if (plugin && plugin->getName() == pluginName) {
            return plugin->onCommand(cmd, arg);
        }
    }
    LOG_WARN("[PluginManager] pluginCommand: no plugin named '%s'", pluginName.c_str());
    return false;
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

bool PluginManager::reloadPluginSettings(const std::string& name) {
    for (auto& plugin : m_plugins) {
        if (plugin->getName() == name) {
            // Plugins read the global PluginConfig directly; the path is
            // only a fallback for file-based setters.
            loadPluginSettings(*plugin);
            return true;
        }
    }
    LOG_WARN("[PluginManager] reloadPluginSettings: no plugin named '%s'", name.c_str());
    return false;
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

bool PluginManager::captureViewPixels(View* view,
                                       std::vector<uint32_t>& pixels,
                                       int* w, int* h) {
    if (!m_server || !view || !w || !h) return false;
    uint32_t* raw = nullptr;
    int rw = 0, rh = 0;
    if (!havel_wlr_capture_view_pixels(view->nativeHandle(), &raw, &rw, &rh)) {
        return false;
    }
    pixels.assign(raw, raw + (size_t)rw * rh);
    havel_wlr_capture_pixels_free(raw);
    *w = rw;
    *h = rh;
    return true;
}

void PluginManager::closeView(View* view) {
    if (!m_server || !view) return;
    // Route the close request through the C bridge — same path as
    // Server::closeFocusedWindow. The compositor still gets the
    // view-destroyed sequence afterwards.
    if (view->nativeHandle()) {
        havel_wlr_close_view(view->nativeHandle());
    }
}

void PluginManager::minimizeView(View* view) {
    if (!m_server || !view) return;
    // Same route as closeView: nativeHandle is the C-side wrapper struct,
    // never the C++ View. (ServerDecorationPlugin previously passed the
    // C++ View* straight to havel_wlr_minimize_view — wrong type.)
    if (view->nativeHandle()) {
        havel_wlr_minimize_view(view->nativeHandle());
    }
}

void PluginManager::toggleMaximizeView(View* view) {
    if (!m_server || !view) return;
    auto* server = static_cast<Server*>(m_server);
    if (Window* window = server->coreWindowManager().getWindow(view)) {
        server->coreWindowManager().toggleMaximize(window);
    }
}

void PluginManager::moveViewToWorkspace(View* view, uint32_t workspaceId) {
    if (!m_server || !view) return;
    auto* server = static_cast<Server*>(m_server);
    server->moveViewToWorkspace(view, workspaceId);
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
    (void)m_server;
    return view->geom().x;
}

int PluginManager::getViewY(View* view) {
    if (!m_server || !view) return 0;
    (void)m_server;
    return view->geom().y;
}

int PluginManager::getViewWidth(View* view) {
    if (!m_server || !view) return 0;
    (void)m_server;
    return view->geom().w;
}

int PluginManager::getViewHeight(View* view) {
    if (!m_server || !view) return 0;
    (void)m_server;
    return view->geom().h;
}

bool PluginManager::isViewFloating(View* view) {
    if (!m_server || !view) return false;
    (void)m_server;
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
    if (!m_server) {
        LOG_WARN("[PluginManager] setZoomForOutput(%d, %.2f) refused: no server",
                 output_index, zoom);
        return;
    }
    auto* server = static_cast<Server*>(m_server);
    void* native = server->nativeHandle();
    if (!native) {
        LOG_WARN("[PluginManager] setZoomForOutput(%d, %.2f) refused: compositor not up",
                 output_index, zoom);
        return;
    }
    // The C frame loop renders zoom as output scale + cursor-centred scene
    // offset (see output_frame in wlr_bridge.c); -1 centre = follow cursor.
    havel_wlr_set_zoom_for_output((havel_wlr_server_t*)native, output_index, zoom,
                                  -1.0f, -1.0f);
}

// ============================================================================
// Zoom (targets live in Server; plugin animates + draws)
// ============================================================================

void PluginManager::zoomConfigure(const std::string& json) {
    if (!m_server) return;
    static_cast<Server*>(m_server)->zoomConfigure(json);
}

void PluginManager::zoomIn(int output_index) {
    if (m_server) static_cast<Server*>(m_server)->zoomIn(output_index);
}

void PluginManager::zoomOut(int output_index) {
    if (m_server) static_cast<Server*>(m_server)->zoomOut(output_index);
}

void PluginManager::zoomReset(int output_index) {
    if (m_server) static_cast<Server*>(m_server)->zoomReset(output_index);
}

void PluginManager::zoomToggle(int output_index) {
    if (m_server) static_cast<Server*>(m_server)->zoomToggle(output_index);
}

void PluginManager::zoomPushStart(int output_index) {
    if (m_server) static_cast<Server*>(m_server)->zoomPushStart(output_index);
}

void PluginManager::zoomPushEnd(int output_index) {
    if (m_server) static_cast<Server*>(m_server)->zoomPushEnd(output_index);
}

void PluginManager::zoomSetMode(int mode) {
    if (m_server) static_cast<Server*>(m_server)->zoomSetMode(mode);
}

float PluginManager::zoomTargetLevel(int output_index) {
    if (!m_server) return 1.0f;
    return static_cast<Server*>(m_server)->zoomTargetLevel(output_index);
}

float PluginManager::zoomTargetMag(int output_index) {
    if (!m_server) return 1.0f;
    return static_cast<Server*>(m_server)->zoomTargetMag(output_index);
}

bool PluginManager::zoomActive(int output_index) {
    if (!m_server) return false;
    return static_cast<Server*>(m_server)->zoomActive(output_index);
}

int PluginManager::zoomMode() {
    if (!m_server) return 0;
    return static_cast<Server*>(m_server)->zoomMode();
}

void PluginManager::zoomRequestCapture(int output_index, float cx, float cy,
                                       int src_w, int src_h) {
    if (m_server)
        static_cast<Server*>(m_server)->zoomRequestCapture(output_index, cx, cy, src_w, src_h);
}

bool PluginManager::zoomGetCapture(int output_index, const uint32_t** pixels, int* w, int* h,
                                   float* src_x, float* src_y, uint64_t* seq) {
    if (!m_server) return false;
    return static_cast<Server*>(m_server)->zoomGetCapture(output_index, pixels, w, h,
                                                          src_x, src_y, seq);
}

void PluginManager::setOutputGeometry(uint32_t workspaceId, int x, int y, int w, int h) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    server->setOutputGeometry(workspaceId, Rect{x, y, w, h});
}

void PluginManager::scheduleRedraw() {
    // Handled by frame loop in C layer
}

CompositorAPI::OutputTarget PluginManager::currentOutputTarget() {
    CompositorAPI::OutputTarget out;
    if (!m_server) return out;
    auto* server = static_cast<Server*>(m_server);
    const auto& t = server->overlayTarget();
    out.x = t.x;
    out.y = t.y;
    out.w = t.w;
    out.h = t.h;
    out.index = t.index;
    out.primary = t.primary;
    out.underCursor = t.underCursor;
    return out;
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
