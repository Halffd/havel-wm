// Zoom Plugin - Per-monitor zoom control
// Accessibility feature for users who need larger UI

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <cstdio>

namespace havel {

/**
 * Zoom Plugin
 *
 * Controls per-monitor zoom for accessibility.
 * 
 * Keybindings:
 * - Meta+Ctrl+Plus: Increase zoom on primary monitor
 * - Meta+Ctrl+Minus: Decrease zoom on primary monitor
 * - Meta+Ctrl+0: Reset zoom on primary monitor
 * - Meta+Shift+Ctrl+Plus: Increase zoom on secondary monitor
 * - Meta+Shift+Ctrl+Minus: Decrease zoom on secondary monitor
 * - Meta+Shift+Ctrl+0: Reset zoom on secondary monitor
 */
class ZoomPlugin : public Plugin {
public:
    const char* name() const override { return "zoom"; }
    const char* version() const override { return "1.0.0"; }

    void init(CompositorAPI* api) override {
        m_api = api;
        m_zoomLevels[0] = 1.0f;  // Primary monitor
        m_zoomLevels[1] = 1.0f;  // Secondary monitor
        
        printf("[ZoomPlugin] Initialized (primary=%.2f, secondary=%.2f)\n",
               m_zoomLevels[0], m_zoomLevels[1]);
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

        if (!event.pressed) return false;
        if (!(event.modifiers & MOD_LOGO)) return false;
        if (!(event.modifiers & MOD_CTRL)) return false;

        // Select which monitor
        int targetOutput = (event.modifiers & MOD_SHIFT) ? 1 : 0;
        const char* targetName = targetOutput == 0 ? "primary" : "secondary";

        // Zoom in: Plus key (keycode 21)
        if (event.keycode == 21) {
            adjustZoom(targetOutput, +0.25f);
            printf("[ZoomPlugin] Zoom %s: %.2f\n", targetName, m_zoomLevels[targetOutput]);
            return true;
        }

        // Zoom out: Minus key (keycode 20)
        if (event.keycode == 20) {
            adjustZoom(targetOutput, -0.25f);
            printf("[ZoomPlugin] Zoom %s: %.2f\n", targetName, m_zoomLevels[targetOutput]);
            return true;
        }

        // Reset zoom: Zero key (keycode 11)
        if (event.keycode == 11) {
            m_zoomLevels[targetOutput] = 1.0f;
            m_api->setZoomForOutput(targetOutput, 1.0f);
            printf("[ZoomPlugin] Zoom %s reset to 1.0\n", targetName);
            return true;
        }

        return false;
    }

private:
    CompositorAPI* m_api = nullptr;
    float m_zoomLevels[2];  // [0] = primary, [1] = secondary

    void adjustZoom(int output_index, float delta) {
        m_zoomLevels[output_index] = clamp(m_zoomLevels[output_index] + delta, 0.5f, 3.0f);
        m_api->setZoomForOutput(output_index, m_zoomLevels[output_index]);
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
