// Zoom Plugin - Per-monitor zoom control with cursor-centered zoom
// Accessibility feature for users who need larger UI

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <cstdio>

namespace havel {

/**
 * Zoom Plugin - Enhanced with cursor-centered zoom and push modes
 *
 * Features:
 * - Per-monitor zoom (0.5x - 3.0x)
 * - Proportional zoom (multiplicative, not additive)
 * - Cursor-centered zoom (zooms toward cursor position)
 * - Push-to-zoom mode (hold key to zoom, release to return)
 * - Toggle zoom mode (press to enable, press to disable)
 *
 * Keybindings:
 * - Meta+Ctrl+Plus: Zoom in (proportional, cursor-centered)
 * - Meta+Ctrl+Minus: Zoom out (proportional, cursor-centered)
 * - Meta+Ctrl+0: Reset zoom
 * - Meta+Ctrl+Z: Toggle zoom (1.0x <-> 2.0x)
 * - Meta+Ctrl+Space: Push-to-zoom (hold to zoom)
 */
class ZoomPlugin : public Plugin {
public:
    const char* name() const override { return "zoom"; }
    const char* version() const override { return "2.0.0"; }

    void init(CompositorAPI* api) override {
        m_api = api;
        
        // Initialize zoom levels
        m_zoomLevels[0] = 1.0f;  // Primary monitor
        m_zoomLevels[1] = 1.0f;  // Secondary monitor
        
        // Saved zoom levels for toggle/push modes
        m_savedZoomLevels[0] = 1.0f;
        m_savedZoomLevels[1] = 1.0f;
        
        // Push-to-zoom state
        m_pushZoomActive = false;
        m_pushZoomLevel = 2.0f;  // Default push zoom level
        
        // Toggle state
        m_toggleZoomActive = false;
        m_toggleZoomLevel = 2.0f;  // Default toggle zoom level
        
        // Cursor position for centered zoom
        m_cursorX = 0;
        m_cursorY = 0;
        
        printf("[ZoomPlugin] Initialized v2.0 (proportional, cursor-centered, push modes)\n");
        printf("[ZoomPlugin]   Primary: %.2f, Secondary: %.2f\n", m_zoomLevels[0], m_zoomLevels[1]);
    }

    void fini() override {
        // Reset zoom on unload
        m_api->setZoomForOutput(0, 1.0f);
        m_api->setZoomForOutput(1, 1.0f);
        printf("[ZoomPlugin] Finalized (reset to defaults)\n");
        m_api = nullptr;
    }

    void loadConfig(const std::string& configPath) override {
        (void)configPath;
        printf("[ZoomPlugin] Config loaded\n");
    }

    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        constexpr uint32_t MOD_SHIFT = 1 << 1;
        constexpr uint32_t MOD_CTRL = 1 << 2;

        if (!(event.modifiers & MOD_LOGO)) return false;
        if (!(event.modifiers & MOD_CTRL)) return false;

        // Select which monitor
        int targetOutput = (event.modifiers & MOD_SHIFT) ? 1 : 0;
        const char* targetName = targetOutput == 0 ? "primary" : "secondary";

        // Get cursor position for centered zoom
        m_cursorX = m_api->getCursorX();
        m_cursorY = m_api->getCursorY();

        // Zoom in: Plus key (keycode 21) - PROPORTIONAL
        if (event.keycode == 21 && event.pressed) {
            zoomInProportional(targetOutput);
            printf("[ZoomPlugin] Zoom %s: %.2f (cursor: %.0f,%.0f)\n", 
                   targetName, m_zoomLevels[targetOutput], m_cursorX, m_cursorY);
            return true;
        }

        // Zoom out: Minus key (keycode 20) - PROPORTIONAL
        if (event.keycode == 20 && event.pressed) {
            zoomOutProportional(targetOutput);
            printf("[ZoomPlugin] Zoom %s: %.2f (cursor: %.0f,%.0f)\n", 
                   targetName, m_zoomLevels[targetOutput], m_cursorX, m_cursorY);
            return true;
        }

        // Reset zoom: Zero key (keycode 11)
        if (event.keycode == 11 && event.pressed) {
            resetZoom(targetOutput);
            printf("[ZoomPlugin] Zoom %s reset to 1.0\n", targetName);
            return true;
        }

        // Toggle zoom: Z key (keycode 44)
        if (event.keycode == 44 && event.pressed) {
            toggleZoom(targetOutput);
            return true;
        }

        // Push-to-zoom: Space key (keycode 57)
        if (event.keycode == 57) {
            if (event.pressed) {
                pushZoomStart(targetOutput);
            } else {
                pushZoomEnd(targetOutput);
            }
            return true;
        }

        return false;
    }

private:
    CompositorAPI* m_api = nullptr;
    
    // Current zoom levels
    float m_zoomLevels[2];  // [0] = primary, [1] = secondary
    
    // Saved zoom levels for toggle/push modes
    float m_savedZoomLevels[2];
    
    // Push-to-zoom state
    bool m_pushZoomActive;
    float m_pushZoomLevel;
    
    // Toggle zoom state
    bool m_toggleZoomActive;
    float m_toggleZoomLevel;
    
    // Cursor position for centered zoom
    double m_cursorX;
    double m_cursorY;

    // Proportional zoom in (multiply by 1.25)
    void zoomInProportional(int output_index) {
        float factor = 1.25f;  // 25% increase
        m_zoomLevels[output_index] = clamp(m_zoomLevels[output_index] * factor, 0.5f, 3.0f);
        m_api->setZoomForOutput(output_index, m_zoomLevels[output_index]);
        
        // Reset toggle/push state when manually adjusting
        m_toggleZoomActive = false;
        m_pushZoomActive = false;
    }

    // Proportional zoom out (divide by 1.25)
    void zoomOutProportional(int output_index) {
        float factor = 0.8f;  // 20% decrease (inverse of 1.25)
        m_zoomLevels[output_index] = clamp(m_zoomLevels[output_index] * factor, 0.5f, 3.0f);
        m_api->setZoomForOutput(output_index, m_zoomLevels[output_index]);
        
        // Reset toggle/push state when manually adjusting
        m_toggleZoomActive = false;
        m_pushZoomActive = false;
    }

    // Reset zoom to 1.0
    void resetZoom(int output_index) {
        m_zoomLevels[output_index] = 1.0f;
        m_api->setZoomForOutput(output_index, 1.0f);
        m_toggleZoomActive = false;
        m_pushZoomActive = false;
    }

    // Toggle zoom between 1.0x and toggle level
    void toggleZoom(int output_index) {
        if (m_toggleZoomActive) {
            // Return to normal
            m_zoomLevels[output_index] = m_savedZoomLevels[output_index];
            m_toggleZoomActive = false;
            printf("[ZoomPlugin] Toggle zoom OFF (restored to %.2f)\n", 
                   m_zoomLevels[output_index]);
        } else {
            // Save current and apply toggle zoom
            m_savedZoomLevels[output_index] = m_zoomLevels[output_index];
            m_zoomLevels[output_index] = m_toggleZoomLevel;
            m_toggleZoomActive = true;
            printf("[ZoomPlugin] Toggle zoom ON (%.2f)\n", m_toggleZoomLevel);
        }
        m_api->setZoomForOutput(output_index, m_zoomLevels[output_index]);
        m_pushZoomActive = false;
    }

    // Push-to-zoom start (hold key)
    void pushZoomStart(int output_index) {
        m_savedZoomLevels[output_index] = m_zoomLevels[output_index];
        m_zoomLevels[output_index] = m_pushZoomLevel;
        m_pushZoomActive = true;
        m_api->setZoomForOutput(output_index, m_zoomLevels[output_index]);
        m_toggleZoomActive = false;
        printf("[ZoomPlugin] Push zoom START (%.2f)\n", m_pushZoomLevel);
    }

    // Push-to-zoom end (release key)
    void pushZoomEnd(int output_index) {
        if (m_pushZoomActive) {
            m_zoomLevels[output_index] = m_savedZoomLevels[output_index];
            m_pushZoomActive = false;
            m_api->setZoomForOutput(output_index, m_zoomLevels[output_index]);
            printf("[ZoomPlugin] Push zoom END (restored to %.2f)\n", 
                   m_zoomLevels[output_index]);
        }
    }

    template<typename T>
    T clamp(T value, T min, T max) {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }
};

// Plugin factory
Plugin* create_zoom_plugin() {
    return new ZoomPlugin();
}

} // namespace havel
