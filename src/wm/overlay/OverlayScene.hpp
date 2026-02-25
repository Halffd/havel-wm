#pragma once

#include <wm/render_c.h>
#include <wm/Types.hpp>
#include <vector>

namespace havel {

/**
 * Overlay scene wrapper around C implementation
 */
class OverlayScene {
public:
    OverlayScene();
    ~OverlayScene();
    
    bool initialize(void* rootScene);
    void cleanup();
    
    void* createOverlay(havel_overlay_type_t type, int x, int y, int width, int height, bool centered);
    void destroyOverlay(void* overlay);
    
    void showOverlay(void* overlay);
    void hideOverlay(void* overlay);
    void toggleOverlay(void* overlay);
    
    void* getOverlay(havel_overlay_type_t type);
    
    bool hasVisibleOverlay() const;
    void* getActiveOverlay() const;
    
    void setOverlayOpacity(void* overlay, float opacity);
    void centerOverlay(void* overlay);
    
    void* rootTree() const { return m_overlayRoot; }
    
private:
    havel_overlay_scene_t* m_overlayRoot = nullptr;
    
    struct OverlayInfo {
        void* tree;
        havel_overlay_type_t type;
        int x, y, width, height;
        bool visible;
    };
    
    std::vector<OverlayInfo> m_overlayInfos;
    std::vector<void*> m_overlays;
};

/**
 * High-level overlay manager
 */
class OverlayManager {
public:
    OverlayManager();
    ~OverlayManager();
    
    bool initialize(void* rootScene);
    void cleanup();
    
    OverlayScene& scene() { return m_scene; }
    const OverlayScene& scene() const { return m_scene; }
    
    void* createAltTab();
    void* createOverview();
    void* createLauncher();
    void* createDebugOverlay();
    
    void toggleAltTab();
    void toggleOverview();
    void toggleLauncher();
    
    void closeAll();
    bool isOverlayActive() const;
    
private:
    OverlayScene m_scene;
    
    void* m_altTabOverlay = nullptr;
    void* m_overviewOverlay = nullptr;
    void* m_launcherOverlay = nullptr;
    void* m_debugOverlay = nullptr;
};

} // namespace havel
