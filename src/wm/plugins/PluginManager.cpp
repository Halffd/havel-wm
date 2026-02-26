#include "PluginManager.hpp"
#include <wm/Server.hpp>
#include <cstring>
#include <cstdio>

namespace havel {

PluginManager::PluginManager() = default;

PluginManager::~PluginManager() {
    shutdown();
}

void PluginManager::initialize(void* server) {
    if (m_initialized) {
        return;
    }
    
    m_server = server;
    m_initialized = true;
    
    printf("[PluginManager] Initialized with %zu plugins\n", m_plugins.size());
    
    // Initialize all registered plugins
    for (auto& plugin : m_plugins) {
        printf("[PluginManager] Initializing plugin: %s\n", plugin->name());
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
        return;
    }
    
    printf("[PluginManager] Registered plugin: %s\n", plugin->name());
    m_plugins.push_back(std::move(plugin));
    
    // If already initialized, init the new plugin immediately
    if (m_initialized && m_server) {
        m_plugins.back()->init(this);
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

void PluginManager::dispatchRenderOverlay(void* renderPass) {
    for (auto& plugin : m_plugins) {
        plugin->renderOverlay(renderPass);
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
    // Would need closeFocusedWindow() in Server
    // For now, stub
    (void)view;
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
    // For now, stub
    (void)view;
    (void)alpha;
}

void PluginManager::setBackgroundColor(float r, float g, float b) {
    if (!m_server) return;
    auto* server = static_cast<Server*>(m_server);
    server->setBackgroundColor(r, g, b);
}

void PluginManager::scheduleRedraw() {
    // Would signal output to redraw
    // For now, stub
}

int PluginManager::getOutputWidth() {
    return 1920;  // Would get from actual output
}

int PluginManager::getOutputHeight() {
    return 1080;  // Would get from actual output
}

} // namespace havel
