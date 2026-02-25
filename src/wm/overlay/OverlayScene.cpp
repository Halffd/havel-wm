#include "OverlayScene.hpp"
#include <wm/render_c.h>
#include <cstdio>
#include <algorithm>

namespace havel {

// ============================================================================
// OverlayScene Implementation
// ============================================================================

OverlayScene::OverlayScene() = default;

OverlayScene::~OverlayScene() {
    cleanup();
}

bool OverlayScene::initialize(void* rootScene) {
    if (!rootScene) {
        return false;
    }
    
    m_overlayRoot = havel_overlay_scene_create(static_cast<struct wlr_scene*>(rootScene));
    
    if (m_overlayRoot) {
        printf("[Overlay] Initialized overlay scene tree\n");
        return true;
    }
    
    return false;
}

void OverlayScene::cleanup() {
    for (auto& info : m_overlayInfos) {
        if (info.tree) {
            havel_overlay_destroy_wrapper(static_cast<havel_overlay_scene_t*>(info.tree));
        }
    }
    
    if (m_overlayRoot) {
        havel_overlay_scene_destroy(m_overlayRoot);
        m_overlayRoot = nullptr;
    }
    
    m_overlayInfos.clear();
    m_overlays.clear();
}

void* OverlayScene::createOverlay(havel_overlay_type_t type, int x, int y, int width, int height, bool centered) {
    if (!m_overlayRoot) {
        return nullptr;
    }
    
    void* tree = havel_overlay_create(type, x, y, width, height, centered);
    if (!tree) {
        return nullptr;
    }
    
    OverlayInfo info;
    info.tree = tree;
    info.type = type;
    info.x = x;
    info.y = y;
    info.width = width;
    info.height = height;
    info.visible = true;
    
    m_overlayInfos.push_back(info);
    m_overlays.push_back(tree);
    
    printf("[Overlay] Created overlay type %d at (%d, %d) %dx%d\n",
           static_cast<int>(type), x, y, width, height);
    
    return tree;
}

void OverlayScene::destroyOverlay(void* overlay) {
    if (!overlay) {
        return;
    }
    
    auto it = m_overlayInfos.begin();
    while (it != m_overlayInfos.end()) {
        if (it->tree == overlay) {
            havel_overlay_destroy_wrapper(static_cast<havel_overlay_scene_t*>(overlay));
            it = m_overlayInfos.erase(it);
        } else {
            ++it;
        }
    }
    
    auto overlayIt = std::find(m_overlays.begin(), m_overlays.end(), overlay);
    if (overlayIt != m_overlays.end()) {
        m_overlays.erase(overlayIt);
    }
}

void OverlayScene::showOverlay(void* overlay) {
    if (!overlay) {
        return;
    }
    
    havel_overlay_show(static_cast<havel_overlay_scene_t*>(overlay));
    
    for (auto& info : m_overlayInfos) {
        if (info.tree == overlay) {
            info.visible = true;
            break;
        }
    }
}

void OverlayScene::hideOverlay(void* overlay) {
    if (!overlay) {
        return;
    }
    
    havel_overlay_hide(static_cast<havel_overlay_scene_t*>(overlay));
    
    for (auto& info : m_overlayInfos) {
        if (info.tree == overlay) {
            info.visible = false;
            break;
        }
    }
}

void OverlayScene::toggleOverlay(void* overlay) {
    if (!overlay) {
        return;
    }
    
    havel_overlay_toggle(static_cast<havel_overlay_scene_t*>(overlay));
}

void* OverlayScene::getOverlay(havel_overlay_type_t type) {
    for (const auto& info : m_overlayInfos) {
        if (info.type == type) {
            return info.tree;
        }
    }
    return nullptr;
}

bool OverlayScene::hasVisibleOverlay() const {
    for (const auto& info : m_overlayInfos) {
        if (info.visible) {
            return true;
        }
    }
    return havel_overlay_is_any_visible();
}

void* OverlayScene::getActiveOverlay() const {
    for (auto it = m_overlayInfos.rbegin(); it != m_overlayInfos.rend(); ++it) {
        if (it->visible) {
            return it->tree;
        }
    }
    return nullptr;
}

void OverlayScene::setOverlayOpacity(void* overlay, float opacity) {
    (void)overlay;
    (void)opacity;
    // Would be handled by render pipeline effects
}

void OverlayScene::centerOverlay(void* overlay) {
    if (!overlay) {
        return;
    }
    
    // Find overlay info to get size
    int overlayWidth = 400;
    int overlayHeight = 300;
    
    for (const auto& info : m_overlayInfos) {
        if (info.tree == overlay) {
            overlayWidth = info.width;
            overlayHeight = info.height;
            break;
        }
    }
    
    int screenWidth = 1920;
    int screenHeight = 1080;
    
    int x = (screenWidth - overlayWidth) / 2;
    int y = (screenHeight - overlayHeight) / 2;
    
    // Would set position via C wrapper
    (void)x;
    (void)y;
}

// ============================================================================
// OverlayManager Implementation
// ============================================================================

OverlayManager::OverlayManager() = default;

OverlayManager::~OverlayManager() {
    cleanup();
}

bool OverlayManager::initialize(void* rootScene) {
    if (!m_scene.initialize(rootScene)) {
        return false;
    }
    
    // Create standard overlays (hidden by default)
    m_altTabOverlay = m_scene.createOverlay(
        HAVEL_OVERLAY_ALT_TAB, 0, 0, 400, 300, true);
    
    m_overviewOverlay = m_scene.createOverlay(
        HAVEL_OVERLAY_OVERVIEW, 0, 0, 0, 0, true);
    
    m_launcherOverlay = m_scene.createOverlay(
        HAVEL_OVERLAY_LAUNCHER, 0, 0, 500, 400, true);
    
    m_debugOverlay = m_scene.createOverlay(
        HAVEL_OVERLAY_DEBUG, 10, 10, 300, 200, false);
    
    // Hide all initially
    if (m_altTabOverlay) m_scene.hideOverlay(m_altTabOverlay);
    if (m_overviewOverlay) m_scene.hideOverlay(m_overviewOverlay);
    if (m_launcherOverlay) m_scene.hideOverlay(m_launcherOverlay);
    if (m_debugOverlay) m_scene.hideOverlay(m_debugOverlay);
    
    printf("[Overlay] Overlay manager initialized\n");
    return true;
}

void OverlayManager::cleanup() {
    m_scene.cleanup();
    m_altTabOverlay = nullptr;
    m_overviewOverlay = nullptr;
    m_launcherOverlay = nullptr;
    m_debugOverlay = nullptr;
}

void* OverlayManager::createAltTab() {
    return m_altTabOverlay;
}

void* OverlayManager::createOverview() {
    return m_overviewOverlay;
}

void* OverlayManager::createLauncher() {
    return m_launcherOverlay;
}

void* OverlayManager::createDebugOverlay() {
    return m_debugOverlay;
}

void OverlayManager::toggleAltTab() {
    if (m_altTabOverlay) {
        m_scene.toggleOverlay(m_altTabOverlay);
    }
}

void OverlayManager::toggleOverview() {
    if (m_overviewOverlay) {
        m_scene.toggleOverlay(m_overviewOverlay);
    }
}

void OverlayManager::toggleLauncher() {
    if (m_launcherOverlay) {
        m_scene.toggleOverlay(m_launcherOverlay);
    }
}

void OverlayManager::closeAll() {
    if (m_altTabOverlay) m_scene.hideOverlay(m_altTabOverlay);
    if (m_overviewOverlay) m_scene.hideOverlay(m_overviewOverlay);
    if (m_launcherOverlay) m_scene.hideOverlay(m_launcherOverlay);
}

bool OverlayManager::isOverlayActive() const {
    return m_scene.hasVisibleOverlay();
}

} // namespace havel
