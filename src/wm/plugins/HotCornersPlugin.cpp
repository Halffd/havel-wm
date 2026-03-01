// Hot Corners Plugin - triggers actions when cursor hits screen corners
// Demonstrates cursor tracking and action execution

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <cstdio>
#include <cstdint>
#include <chrono>

namespace havel     {

// Get current time in milliseconds
static uint64_t getMonotonicTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

/**
 * Hot Corners Plugin
 * 
 * Triggers actions when cursor enters screen corners.
 * Similar to GNOME Activities or macOS Mission Control.
 * 
 * Features:
 * - Configurable actions per corner
 * - Delay before trigger (prevents accidental triggers)
 * - Visual feedback (future)
 * 
 * Default actions:
 * - Top-Left: Show overview (scale)
 * - Top-Right: Show all windows
 * - Bottom-Left: Show desktop
 * - Bottom-Right: Disabled
 */
class HotCornersPlugin : public Plugin {
public:
    const char* name() const override { return "hot_corners"; }
    const char* version() const override { return "0.1.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        m_enabled = true;
        m_triggerDelay = 250;  // ms
        m_lastTriggerTime = 0;
        
        // Default corner actions
        m_cornerActions[0] = ACTION_OVERVIEW;    // Top-left
        m_cornerActions[1] = ACTION_WINDOWS;     // Top-right
        m_cornerActions[2] = ACTION_DESKTOP;     // Bottom-left
        m_cornerActions[3] = ACTION_NONE;        // Bottom-right
        
        m_lastCorner = -1;
        m_cornerEnterTime = 0;
        
        printf("[HotCorners] Initialized (delay: %dms)\n", m_triggerDelay);
    }
    
    void fini() override {
        printf("[HotCorners] Finalized\n");
        m_api = nullptr;
    }
    
    void loadConfig(const std::string& configPath) override {
        // Would load corner actions, delay, etc.
        (void)configPath;
        printf("[HotCorners] Config loaded\n");
    }
    
    void onOutputFrame(const OutputFrameEvent& event) override {
        if (!m_enabled) return;
        
        // Get cursor position from API
        int cursorX = (int)m_api->getCursorX();
        int cursorY = (int)m_api->getCursorY();
        
        // Get output dimensions
        int width = m_api->getOutputWidth();
        int height = m_api->getOutputHeight();
        
        // Detect which corner cursor is in
        Corner corner = detectCorner(cursorX, cursorY, width, height);
        
        // If cursor moved to a new corner
        if (corner != m_lastCorner) {
            if (m_lastCorner >= 0) {
                // Cursor left previous corner - reset
                m_cornerEnterTime = 0;
            }
            m_lastCorner = corner;
            m_cornerEnterTime = getMonotonicTimeMs();
        } else if (m_cornerEnterTime > 0) {
            // Check if we've been in the corner long enough to trigger
            uint64_t elapsed = getMonotonicTimeMs() - m_cornerEnterTime;
            if (elapsed >= m_triggerDelay) {
                triggerAction(m_cornerActions[(int)corner]);
                m_cornerEnterTime = 0;  // Reset to prevent repeated triggers
            }
        }
    }
    
    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        
        if (!event.pressed) return false;
        
        // Meta+C toggles hot corners
        if ((event.modifiers & MOD_LOGO) && event.keycode == 34) {
            m_enabled = !m_enabled;
            printf("[HotCorners] %s\n", m_enabled ? "enabled" : "disabled");
            return true;
        }
        
        // Manual trigger shortcuts
        if ((event.modifiers & MOD_LOGO)) {
            switch (event.keycode) {
                case 32:  // Meta+W - Trigger top-left (overview)
                    triggerAction(ACTION_OVERVIEW);
                    return true;
                case 33:  // Meta+E - Trigger top-right (windows)
                    triggerAction(ACTION_WINDOWS);
                    return true;
            }
        }
        
        return false;
    }
    
private:
    CompositorAPI* m_api = nullptr;
    bool m_enabled;
    uint32_t m_triggerDelay;  // ms before trigger
    uint64_t m_lastTriggerTime;
    int m_lastCorner;
    uint64_t m_cornerEnterTime;
    
    enum CornerAction {
        ACTION_NONE = 0,
        ACTION_OVERVIEW,    // Show overview/scale
        ACTION_WINDOWS,     // Show all windows
        ACTION_DESKTOP,     // Show desktop (minimize all)
        ACTION_WORKSPACE_1, // Switch to workspace 1
        ACTION_WORKSPACE_2, // Switch to workspace 2
    };
    
    CornerAction m_cornerActions[4];  // TL, TR, BL, BR
    
    enum Corner {
        CORNER_TOP_LEFT = 0,
        CORNER_TOP_RIGHT = 1,
        CORNER_BOTTOM_LEFT = 2,
        CORNER_BOTTOM_RIGHT = 3
    };
    
    Corner detectCorner(int x, int y, int width, int height) {
        int threshold = 5;  // pixels from edge
        
        bool top = y < threshold;
        bool bottom = y > height - threshold;
        bool left = x < threshold;
        bool right = x > width - threshold;
        
        if (top && left) return CORNER_TOP_LEFT;
        if (top && right) return CORNER_TOP_RIGHT;
        if (bottom && left) return CORNER_BOTTOM_LEFT;
        if (bottom && right) return CORNER_BOTTOM_RIGHT;
        
        return CORNER_TOP_LEFT;  // Default
    }
    
    void triggerAction(CornerAction action) {
        uint64_t currentTime = getMonotonicTimeMs();
        if (currentTime - m_lastTriggerTime < 500) {
            return;  // Debounce
        }
        
        m_lastTriggerTime = currentTime;
        
        switch (action) {
            case ACTION_OVERVIEW:
                printf("[HotCorners] Trigger: Overview\n");
                // Would trigger scale plugin
                break;
            case ACTION_WINDOWS:
                printf("[HotCorners] Trigger: All Windows\n");
                // Would show all windows view
                break;
            case ACTION_DESKTOP:
                printf("[HotCorners] Trigger: Show Desktop\n");
                // Would minimize all windows
                break;
            case ACTION_WORKSPACE_1:
                printf("[HotCorners] Trigger: Workspace 1\n");
                m_api->setActiveWorkspace(0);
                break;
            case ACTION_WORKSPACE_2:
                printf("[HotCorners] Trigger: Workspace 2\n");
                m_api->setActiveWorkspace(1);
                break;
            case ACTION_NONE:
                break;
        }
    }
};

// Plugin factory
Plugin* create_hot_corners_plugin() {
    return new HotCornersPlugin();
}

} // namespace havel
