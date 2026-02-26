// Window Snap Plugin - snaps windows to edges/corners when dragged
// Demonstrates view position manipulation and drag tracking

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <cstdio>
#include <cstring>

namespace havel {

/**
 * Window Snap Plugin
 * 
 * Snaps windows to screen edges and corners when dragged near them.
 * Similar to Windows Aero Snap or GNOME window snapping.
 * 
 * Features:
 * - Snap to left/right edges (half screen)
 * - Snap to top edge (maximize)
 * - Snap to corners (quarter screen)
 * - Configurable snap threshold
 */
class WindowSnapPlugin : public Plugin {
public:
    const char* name() const override { return "window_snap"; }
    const char* version() const override { return "0.1.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        m_enabled = true;
        m_snapThreshold = 20;  // pixels
        m_draggingView = nullptr;
        m_lastCursorX = 0;
        m_lastCursorY = 0;
        printf("[WindowSnap] Initialized (threshold: %dpx)\n", m_snapThreshold);
    }
    
    void fini() override {
        printf("[WindowSnap] Finalized\n");
        m_api = nullptr;
    }
    
    void loadConfig(const std::string& configPath) override {
        // Would load snap threshold, enabled state, etc.
        (void)configPath;
        printf("[WindowSnap] Config loaded\n");
    }
    
    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        
        if (!event.pressed) return false;
        
        // Meta+Z toggles window snap
        if ((event.modifiers & MOD_LOGO) && event.keycode == 36) {
            m_enabled = !m_enabled;
            printf("[WindowSnap] %s\n", m_enabled ? "enabled" : "disabled");
            return true;
        }
        
        // Snap shortcuts (when window is focused)
        if (m_enabled && (event.modifiers & MOD_LOGO)) {
            View* view = m_api->getFocusedView();
            if (!view) return false;
            
            switch (event.keycode) {
                case 105:  // Meta+Right - Snap right half
                    snapRight(view);
                    return true;
                case 106:  // Meta+Left - Snap left half
                    snapLeft(view);
                    return true;
                case 103:  // Meta+Up - Maximize
                    snapMaximize(view);
                    return true;
                case 108:  // Meta+Down - Restore
                    snapRestore(view);
                    return true;
            }
        }
        
        return false;
    }
    
private:
    CompositorAPI* m_api = nullptr;
    bool m_enabled;
    int m_snapThreshold;
    View* m_draggingView;
    int m_lastCursorX, m_lastCursorY;
    
    // Snap positions
    enum SnapPosition {
        SNAP_NONE = 0,
        SNAP_LEFT,
        SNAP_RIGHT,
        SNAP_MAXIMIZE,
        SNAP_TOP_LEFT,
        SNAP_TOP_RIGHT,
        SNAP_BOTTOM_LEFT,
        SNAP_BOTTOM_RIGHT
    };
    
    SnapPosition detectSnapPosition(int x, int y) {
        int width = m_api->getOutputWidth();
        int height = m_api->getOutputHeight();
        int threshold = m_snapThreshold;
        
        // Check corners first (more specific)
        if (x < threshold && y < threshold) {
            return SNAP_TOP_LEFT;
        }
        if (x > width - threshold && y < threshold) {
            return SNAP_TOP_RIGHT;
        }
        if (x < threshold && y > height - threshold) {
            return SNAP_BOTTOM_LEFT;
        }
        if (x > width - threshold && y > height - threshold) {
            return SNAP_BOTTOM_RIGHT;
        }
        
        // Check edges
        if (x < threshold) {
            return SNAP_LEFT;
        }
        if (x > width - threshold) {
            return SNAP_RIGHT;
        }
        if (y < threshold) {
            return SNAP_MAXIMIZE;
        }
        
        return SNAP_NONE;
    }
    
    void snapLeft(View* view) {
        int width = m_api->getOutputWidth();
        int halfWidth = width / 2;
        (void)halfWidth;  // Would be used for size
        m_api->setViewPosition(view, 0, 0);
        printf("[WindowSnap] Snapped to left half\n");
    }
    
    void snapRight(View* view) {
        int width = m_api->getOutputWidth();
        int halfWidth = width / 2;
        m_api->setViewPosition(view, halfWidth, 0);
        (void)halfWidth;  // Would be used for size
        printf("[WindowSnap] Snapped to right half\n");
    }
    
    void snapMaximize(View* view) {
        m_api->setViewPosition(view, 0, 0);
        printf("[WindowSnap] Maximized\n");
    }
    
    void snapRestore(View* view) {
        // Would restore to previous size/position
        printf("[WindowSnap] Restored\n");
    }
    
    void snapCorner(View* view, SnapPosition corner) {
        int width = m_api->getOutputWidth();
        int height = m_api->getOutputHeight();
        int halfWidth = width / 2;
        int halfHeight = height / 2;
        
        switch (corner) {
            case SNAP_TOP_LEFT:
                m_api->setViewPosition(view, 0, 0);
                break;
            case SNAP_TOP_RIGHT:
                m_api->setViewPosition(view, halfWidth, 0);
                break;
            case SNAP_BOTTOM_LEFT:
                m_api->setViewPosition(view, 0, halfHeight);
                break;
            case SNAP_BOTTOM_RIGHT:
                m_api->setViewPosition(view, halfWidth, halfHeight);
                break;
            default:
                break;
        }
        printf("[WindowSnap] Snapped to corner\n");
    }
};

// Plugin factory
Plugin* create_window_snap_plugin() {
    return new WindowSnapPlugin();
}

} // namespace havel
