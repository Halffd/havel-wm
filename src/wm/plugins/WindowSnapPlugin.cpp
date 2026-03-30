// Window Snap Plugin - snaps windows to edges/corners when dragged
// Demonstrates view position manipulation and drag tracking

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/render/OverlayRenderer.hpp>
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
    
    void onMouseMotion(int x, int y) override {
        if (!m_enabled) return;
        
        // Track cursor position for snap detection
        m_lastCursorX = x;
        m_lastCursorY = y;
        
        // Check if we should show snap preview
        if (m_draggingView) {
            SnapPosition snap = detectSnapPosition(x, y);
            if (snap != SNAP_NONE && snap != m_currentSnapPreview) {
                m_currentSnapPreview = snap;
                m_api->scheduleRedraw();  // Trigger overlay redraw
            } else if (snap == SNAP_NONE && m_currentSnapPreview != SNAP_NONE) {
                m_currentSnapPreview = SNAP_NONE;
                m_api->scheduleRedraw();
            }
        }
    }
    
    void renderOverlay(void* rendererPtr) override {
        if (!m_enabled || !rendererPtr || m_currentSnapPreview == SNAP_NONE) return;
        
        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
        int screenWidth = renderer->getScreenWidth();
        int screenHeight = renderer->getScreenHeight();
        
        // Calculate preview rectangle based on snap position
        FloatRect previewRect = getSnapPreviewRect(screenWidth, screenHeight);
        
        // Draw semi-transparent preview rectangle
        Color previewColor(0.2f, 0.6f, 1.0f, 0.5f);  // Blue with 50% alpha
        Color borderColor(0.2f, 0.6f, 1.0f, 1.0f);    // Solid blue border
        
        renderer->drawRect(previewRect.x, previewRect.y, previewRect.w, previewRect.h, previewColor);
        renderer->drawBorder(previewRect, borderColor, 3.0f);
    }
    
private:
    CompositorAPI* m_api = nullptr;
    bool m_enabled;
    int m_snapThreshold;
    View* m_draggingView;
    int m_lastCursorX, m_lastCursorY;
    
    // Snap positions enum (must be declared before member variables that use it)
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
    
    SnapPosition m_currentSnapPreview = SNAP_NONE;  // Current snap preview zone
    
    // Geometry tracking for restore
    struct SavedGeometry {
        int x, y, width, height;
        bool saved = false;
    };
    SavedGeometry m_lastGeometry;

    // Snap position detection
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
    
    void saveGeometry(View* view) {
        if (!view) return;
        m_lastGeometry.x = m_api->getViewX(view);
        m_lastGeometry.y = m_api->getViewY(view);
        m_lastGeometry.width = m_api->getViewWidth(view);
        m_lastGeometry.height = m_api->getViewHeight(view);
        m_lastGeometry.saved = true;
    }
    
    // Calculate preview rectangle for snap zones
    FloatRect getSnapPreviewRect(int screenWidth, int screenHeight) {
        int width = screenWidth;
        int height = screenHeight;
        int halfWidth = width / 2;
        int quarterWidth = width / 4;
        int halfHeight = height / 2;
        
        // Margin from edge for visual feedback
        int margin = 20;
        
        switch (m_currentSnapPreview) {
            case SNAP_LEFT:
                return FloatRect(margin, margin, halfWidth - margin * 2, height - margin * 2);
            
            case SNAP_RIGHT:
                return FloatRect(halfWidth + margin, margin, halfWidth - margin * 2, height - margin * 2);
            
            case SNAP_MAXIMIZE:
                return FloatRect(margin, margin, width - margin * 2, height - margin * 2);
            
            case SNAP_TOP_LEFT:
                return FloatRect(margin, margin, quarterWidth - margin, halfHeight - margin * 2);
            
            case SNAP_TOP_RIGHT:
                return FloatRect(halfWidth + margin, margin, quarterWidth - margin, halfHeight - margin * 2);
            
            case SNAP_BOTTOM_LEFT:
                return FloatRect(margin, halfHeight + margin, quarterWidth - margin, halfHeight - margin * 2);
            
            case SNAP_BOTTOM_RIGHT:
                return FloatRect(halfWidth + margin, halfHeight + margin, quarterWidth - margin, halfHeight - margin * 2);
            
            default:
                return FloatRect(0, 0, 0, 0);
        }
    }

    void snapLeft(View* view) {
        int width = m_api->getOutputWidth();
        int height = m_api->getOutputHeight();
        int halfWidth = width / 2;

        // Save current geometry before snapping
        saveGeometry(view);

        // REAL snap: set position AND size
        m_api->setViewGeometry(view, 0, 0, halfWidth, height);
        printf("[WindowSnap] Snapped to left half (%dx%d)\n", halfWidth, height);
        
        // Clear preview after snap
        m_currentSnapPreview = SNAP_NONE;
        m_api->scheduleRedraw();
    }

    void snapRight(View* view) {
        int width = m_api->getOutputWidth();
        int height = m_api->getOutputHeight();
        int halfWidth = width / 2;

        // Save current geometry before snapping
        saveGeometry(view);

        // REAL snap: set position AND size
        m_api->setViewGeometry(view, halfWidth, 0, halfWidth, height);
        printf("[WindowSnap] Snapped to right half (%dx%d at %d,0)\n", halfWidth, height, halfWidth);
        
        // Clear preview after snap
        m_currentSnapPreview = SNAP_NONE;
        m_api->scheduleRedraw();
    }

    void snapMaximize(View* view) {
        int width = m_api->getOutputWidth();
        int height = m_api->getOutputHeight();

        // Save current geometry before maximizing
        saveGeometry(view);

        // REAL maximize: full screen
        m_api->setViewGeometry(view, 0, 0, width, height);
        printf("[WindowSnap] Maximized (%dx%d)\n", width, height);
        
        // Clear preview after snap
        m_currentSnapPreview = SNAP_NONE;
        m_api->scheduleRedraw();
    }

    void snapRestore(View* view) {
        // Restore saved geometry if available
        if (m_lastGeometry.saved && view) {
            m_api->setViewGeometry(view, m_lastGeometry.x, m_lastGeometry.y,
                                   m_lastGeometry.width, m_lastGeometry.height);
            printf("[WindowSnap] Restored to %dx%d at %d,%d\n",
                   m_lastGeometry.width, m_lastGeometry.height,
                   m_lastGeometry.x, m_lastGeometry.y);
            m_lastGeometry.saved = false;
        } else {
            printf("[WindowSnap] No geometry to restore\n");
        }
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
        
        // Clear preview after snap
        m_currentSnapPreview = SNAP_NONE;
        m_api->scheduleRedraw();
    }
};

// Plugin factory
Plugin* create_window_snap_plugin() {
    return new WindowSnapPlugin();
}

} // namespace havel
