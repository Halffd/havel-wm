#include "PluginManager.hpp"
#include <wm/Server.hpp>
#include <wm/bridge.h>
#include <Logger.h>
#include <cstring>
#include <cstdio>

namespace havel {

PluginManager::PluginManager() = default;

PluginManager::~PluginManager() {
    shutdown();
}

bool PluginManager::loadConfig(const std::string& path) {
    m_configPath = path;  // Store for hot-reload
    bool loaded = m_config.load(path);
    if (loaded) {
        printf("[PluginManager] Configuration loaded from %s\n", path.c_str());
    } else {
        printf("[PluginManager] No configuration file found at %s\n", path.c_str());
    }
    return loaded;
}

bool PluginManager::reloadConfig() {
    if (m_configPath.empty()) {
        printf("[PluginManager] No config path stored, cannot reload\n");
        return false;
    }

    printf("[PluginManager] Reloading configuration from %s\n", m_configPath.c_str());
    
    // Store currently enabled plugins
    std::vector<std::string> previouslyEnabled;
    for (const auto& plugin : m_plugins) {
        if (m_config.isEnabled(plugin->name())) {
            previouslyEnabled.push_back(plugin->name());
        }
    }

    // Reload config
    bool loaded = m_config.load(m_configPath);
    if (!loaded) {
        printf("[PluginManager] Failed to reload config, keeping previous settings\n");
        return false;
    }

    // Check for plugins that need to be enabled/disabled
    for (const auto& plugin : m_plugins) {
        std::string name = plugin->name();
        bool shouldBeEnabled = m_config.isEnabled(name);
        bool wasEnabled = std::find(previouslyEnabled.begin(), previouslyEnabled.end(), name) != previouslyEnabled.end();

        if (shouldBeEnabled && !wasEnabled) {
            printf("[PluginManager] Enabling plugin: %s\n", name.c_str());
            plugin->init(this);
        } else if (!shouldBeEnabled && wasEnabled) {
            printf("[PluginManager] Disabling plugin: %s\n", name.c_str());
            plugin->fini();
        }
    }

    printf("[PluginManager] Configuration reloaded successfully\n");
    return true;
}

bool PluginManager::isPluginEnabled(const std::string& name) const {
    return m_config.isEnabled(name);
}

void PluginManager::initialize(void* server) {
    if (m_initialized) {
        return;
    }

    m_server = server;
    m_initialized = true;

    printf("[PluginManager] Initialized with %zu plugins\n", m_plugins.size());

    // Initialize all registered plugins (check if enabled)
    for (auto& plugin : m_plugins) {
        std::string name = plugin->name();
        if (!isPluginEnabled(name)) {
            printf("[PluginManager] Skipping disabled plugin: %s\n", name.c_str());
            continue;
        }
        printf("[PluginManager] Initializing plugin: %s\n", name.c_str());
        plugin->init(this);
    }
}

void PluginManager::shutdown() {
    if (!m_initialized) {
        return;
    }
    
    // Finalize all plugins in reverse order
    for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it) {
        printf("[PluginManager] Finalizing plugin: %s\n", (*it)->name());
        (*it)->fini();
    }
    
    m_plugins.clear();
    m_server = nullptr;
    m_initialized = false;
}

void PluginManager::registerPlugin(std::unique_ptr<Plugin> plugin) {
    if (!plugin) {
        printf("[PluginManager] WARNING: null plugin passed to registerPlugin!\n");
        return;
    }

    printf("[PluginManager] Registering plugin: %s (ptr=%p)\n", plugin->name(), (void*)plugin.get());
    m_plugins.push_back(std::move(plugin));

    // If already initialized, init the new plugin immediately (if enabled)
    if (m_initialized && m_server) {
        std::string name = m_plugins.back()->name();
        printf("[PluginManager] Plugin %s enabled=%d\n", name.c_str(), isPluginEnabled(name) ? 1 : 0);
        if (isPluginEnabled(name)) {
            printf("[PluginManager] Initializing newly registered plugin: %s (ptr=%p)\n", name.c_str(), (void*)m_plugins.back().get());
            m_plugins.back()->init(this);
        } else {
            printf("[PluginManager] Skipping disabled plugin: %s\n", name.c_str());
        }
    }
}

void PluginManager::unregisterPlugin(const char* name) {
    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
        if (strcmp((*it)->name(), name) == 0) {
            printf("[PluginManager] Unregistering plugin: %s\n", name);
            (*it)->fini();
            m_plugins.erase(it);
            return;
        }
    }
}

// Event dispatch
void PluginManager::dispatchOutputFrame(const OutputFrameEvent& event) {
    for (auto& plugin : m_plugins) {
        if (!plugin) {
            printf("[PluginManager] WARNING: null plugin in vector!\n");
            continue;
        }
        printf("[PluginManager] dispatchOutputFrame: plugin=%s\n", plugin->name());
        plugin->onOutputFrame(event);
    }
}

void PluginManager::dispatchViewMap(const ViewEvent& event) {
    for (auto& plugin : m_plugins) {
        plugin->onViewMap(event);
    }
}

void PluginManager::dispatchViewUnmap(const ViewEvent& event) {
    for (auto& plugin : m_plugins) {
        plugin->onViewUnmap(event);
    }
}

void PluginManager::dispatchViewDestroy(const ViewEvent& event) {
    for (auto& plugin : m_plugins) {
        plugin->onViewDestroy(event);
    }
}

bool PluginManager::dispatchKey(const KeyEvent& event) {
    for (auto& plugin : m_plugins) {
        if (plugin->onKey(event)) {
            return true;  // Event consumed by plugin
        }
    }
    return false;  // Event not consumed, pass to compositor
}

void PluginManager::renderOverlays(void* renderer) {
    // Overlay rendering via OverlayRenderer (OpenGL)
    // Plugins draw using OverlayRenderer methods (drawRect, drawText, etc.)
    
    // Pass the renderer to plugins - they cast it to OverlayRenderer*
    if (!renderer) return;
    
    for (auto& plugin : m_plugins) {
        plugin->renderOverlay(renderer);
    }
}

void PluginManager::onMouseMotion(int x, int y) {
    // Forward mouse motion to all plugins
    for (auto& plugin : m_plugins) {
        plugin->onMouseMotion(x, y);
    }
}

void PluginManager::onMouseButton(uint32_t button, bool pressed, int x, int y) {
    // Forward mouse button to all plugins
    for (auto& plugin : m_plugins) {
        plugin->onMouseButton(button, pressed, x, y);
    }
}

// CompositorAPI implementation - delegates to Server
View* PluginManager::getFocusedView() {
    if (!m_server) return nullptr;
    auto* server = static_cast<Server*>(m_server);
    auto* ws = server->activeWorkspace();
    return ws ? ws->activeView() : nullptr;
}

void PluginManager::focusView(View* view) {
    if (!m_server || !view) return;
    auto* server = static_cast<Server*>(m_server);
    server->focusView(view);
}

void PluginManager::closeView(View* view) {
    if (!m_server || !view) return;
    
    // Close the view through wlroots
    havel_wlr_close_view(view->nativeHandle());
}

// Window enumeration - delegates to Server
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
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    View* view = server->getViewById(id);
    if (view) {
        server->focusView(view);
    }
}

std::string PluginManager::getViewAppId(View* view) {
    if (!view) return "";
    return view->appId();
}

std::string PluginManager::getViewTitle(View* view) {
    if (!view) return "";
    return view->title();
}

uint32_t PluginManager::getViewTextureId(View* view) {
    if (!view) return 0;
    void* native = view->nativeHandle();
    return havel_get_view_texture_id(native);
}

int PluginManager::getViewTextureWidth(View* view) {
    if (!view) return 0;
    return havel_get_view_texture_width(view->nativeHandle());
}

int PluginManager::getViewTextureHeight(View* view) {
    if (!view) return 0;
    return havel_get_view_texture_height(view->nativeHandle());
}

// Window geometry
int PluginManager::getViewX(View* view) {
    if (!view) return 0;
    return view->geom().x;
}

int PluginManager::getViewY(View* view) {
    if (!view) return 0;
    return view->geom().y;
}

int PluginManager::getViewWidth(View* view) {
    if (!view) return 0;
    return view->geom().w;
}

int PluginManager::getViewHeight(View* view) {
    if (!view) return 0;
    return view->geom().h;
}

bool PluginManager::isViewFloating(View* view) {
    if (!view) return false;
    return view->isFloating();
}

uint32_t PluginManager::getActiveWorkspace() {
    if (!m_server) return 0;
    auto* server = static_cast<Server*>(m_server);
    return server->activeWorkspace()->id();
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
    server->setViewPosition(view, x, y, false);
}

void PluginManager::setViewOpacity(View* view, float alpha) {
    // wlroots 0.20 scene doesn't have per-node opacity
    // This would require scene graph extension
    // Log the request for debugging
    if (view && alpha != 1.0f) {
        LOG_DEBUG("[PluginManager] setViewOpacity requested: %.2f (not supported in wlroots 0.20)", alpha);
    }
    (void)view;
    (void)alpha;
}

void PluginManager::setViewGeometry(View* view, int x, int y, int w, int h) {
    if (!m_server) return;
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

// Per-monitor control
void PluginManager::setGammaForOutput(int output_index, float gamma) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    server->setGamma(gamma);
    void* nativeHandle = server->nativeHandle();
    if (nativeHandle) {
        havel_cpp_set_gamma_for_output(static_cast<struct havel_cpp_server*>(nativeHandle), output_index, gamma);
    }
}

void PluginManager::setTemperatureForOutput(int output_index, int kelvin) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    server->setTemperature(kelvin);
    void* nativeHandle = server->nativeHandle();
    if (nativeHandle) {
        havel_cpp_set_temperature_for_output(static_cast<struct havel_cpp_server*>(nativeHandle), output_index, kelvin);
    }
}

void PluginManager::setBrightnessForOutput(int output_index, float brightness) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    server->setBrightness(brightness);
    void* nativeHandle = server->nativeHandle();
    if (nativeHandle) {
        havel_cpp_set_brightness_for_output(static_cast<struct havel_cpp_server*>(nativeHandle), output_index, brightness);
    }
}

void PluginManager::setZoomForOutput(int output_index, float zoom) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    
    // Get cursor position for cursor-centered zoom
    double cursorX = server->cursorX();
    double cursorY = server->cursorY();
    
    void* nativeHandle = server->nativeHandle();
    if (nativeHandle) {
        havel_cpp_set_zoom_for_output(static_cast<struct havel_cpp_server*>(nativeHandle), 
                                       output_index, zoom, cursorX, cursorY);
    }
}

void PluginManager::scheduleRedraw() {
    // Signal all outputs to redraw
    // This would typically be done through the C bridge
    if (m_server) {
        auto* server = static_cast<Server*>(m_server);
        void* nativeHandle = server->nativeHandle();
        if (nativeHandle) {
            // The C layer handles output redraw signaling
            // For now, this is handled by the frame loop
        }
    }
}

int PluginManager::getOutputWidth() {
    // Get width from primary output
    if (m_server) {
        auto* server = static_cast<Server*>(m_server);
        return server->getOutputWidth();
    }
    return 1920;
}

int PluginManager::getOutputHeight() {
    // Get height from primary output
    if (m_server) {
        auto* server = static_cast<Server*>(m_server);
        return server->getOutputHeight();
    }
    return 1080;
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
