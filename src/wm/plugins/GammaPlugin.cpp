// Gamma/Temperature Plugin - Phase 3.1 Output Control
// Demonstrates gamma, temperature, and brightness control

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <cstdio>
#include <cmath>

namespace havel {

/**
 * Gamma/Temperature Plugin
 * 
 * Controls display gamma, color temperature, and brightness.
 * Useful for:
 * - Reducing eye strain (night mode)
 * - Calibrating displays
 * - Power saving (dimming)
 * 
 * Keybindings:
 * - Meta+PageUp/PageDown: Adjust gamma
 * - Meta+Home/End: Adjust temperature
 * - Meta+Insert/Delete: Adjust brightness
 */
class GammaPlugin : public Plugin {
public:
    const char* name() const override { return "gamma"; }
    const char* version() const override { return "0.1.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        m_gamma = 1.0f;
        m_temperature = 6500;  // Kelvin (D65 white point)
        m_brightness = 1.0f;
        m_nightMode = false;
        
        printf("[GammaPlugin] Initialized (gamma=%.2f, temp=%dK, brightness=%.2f)\n",
               m_gamma, m_temperature, m_brightness);
    }
    
    void fini() override {
        // Reset to defaults on unload
        m_api->setGamma(1.0f);
        m_api->setTemperature(6500);
        m_api->setBrightness(1.0f);
        printf("[GammaPlugin] Finalized (reset to defaults)\n");
        m_api = nullptr;
    }
    
    void loadConfig(const std::string& configPath) override {
        // Would load gamma, temperature, brightness, night mode schedule
        (void)configPath;
        printf("[GammaPlugin] Config loaded\n");
    }
    
    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        
        if (!event.pressed) return false;
        if (!(event.modifiers & MOD_LOGO)) return false;
        
        // Gamma adjustment
        if (event.keycode == 104) {  // PageUp
            adjustGamma(+0.1f);
            return true;
        }
        if (event.keycode == 105) {  // PageDown
            adjustGamma(-0.1f);
            return true;
        }
        
        // Temperature adjustment
        if (event.keycode == 102) {  // Home
            adjustTemperature(+100);
            return true;
        }
        if (event.keycode == 107) {  // End
            adjustTemperature(-100);
            return true;
        }
        
        // Brightness adjustment
        if (event.keycode == 110) {  // Insert
            adjustBrightness(+0.1f);
            return true;
        }
        if (event.keycode == 111) {  // Delete
            adjustBrightness(-0.1f);
            return true;
        }
        
        // Night mode toggle (Meta+N)
        if (event.keycode == 39) {  // N
            toggleNightMode();
            return true;
        }
        
        return false;
    }
    
private:
    CompositorAPI* m_api = nullptr;
    float m_gamma;
    int m_temperature;
    float m_brightness;
    bool m_nightMode;
    
    // Night mode settings
    static constexpr int NIGHT_TEMPERATURE = 3500;  // Warm
    static constexpr int DAY_TEMPERATURE = 6500;    // Cool
    
    void adjustGamma(float delta) {
        m_gamma = clamp(m_gamma + delta, 0.1f, 2.0f);
        m_api->setGamma(m_gamma);
        printf("[GammaPlugin] Gamma: %.2f\n", m_gamma);
    }
    
    void adjustTemperature(int delta) {
        m_temperature = clamp(m_temperature + delta, 3000, 10000);
        m_api->setTemperature(m_temperature);
        printf("[GammaPlugin] Temperature: %dK\n", m_temperature);
    }
    
    void adjustBrightness(float delta) {
        m_brightness = clamp(m_brightness + delta, 0.1f, 1.0f);
        m_api->setBrightness(m_brightness);
        printf("[GammaPlugin] Brightness: %.2f\n", m_brightness);
    }
    
    void toggleNightMode() {
        m_nightMode = !m_nightMode;
        
        if (m_nightMode) {
            m_temperature = NIGHT_TEMPERATURE;  // ← FIX: Update internal state
            m_api->setTemperature(NIGHT_TEMPERATURE);
            printf("[GammaPlugin] Night mode ON (%dK)\n", NIGHT_TEMPERATURE);
        } else {
            m_temperature = DAY_TEMPERATURE;  // ← FIX: Update internal state
            m_api->setTemperature(DAY_TEMPERATURE);
            printf("[GammaPlugin] Night mode OFF (%dK)\n", DAY_TEMPERATURE);
        }
    }
    
    template<typename T>
    T clamp(T value, T min, T max) {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }
    
    // Convert Kelvin to RGB multipliers (simplified)
    void kelvinToRGB(int kelvin, float& r, float& g, float& b) {
        // Simplified temperature to RGB conversion
        // For accurate conversion, use Planckian locus approximation
        r = g = b = 1.0f;
        
        float temp = kelvin / 100.0f;
        
        // Red
        if (temp <= 66.0f) {
            r = 1.0f;
        } else {
            r = 1.294f * std::pow(temp - 60.0f, -0.133f);
            r = clamp(r, 0.0f, 1.0f);
        }
        
        // Green
        if (temp <= 66.0f) {
            g = 0.994f * std::pow(temp, 0.170f);
            g = clamp(g, 0.0f, 1.0f);
        } else {
            g = 1.129f * std::pow(temp - 60.0f, -0.082f);
            g = clamp(g, 0.0f, 1.0f);
        }
        
        // Blue
        if (temp >= 66.0f) {
            b = 1.0f;
        } else if (temp <= 19.0f) {
            b = 0.0f;
        } else {
            b = 0.543f * std::pow(temp - 10.0f, 0.443f);
            b = clamp(b, 0.0f, 1.0f);
        }
    }
};

// Plugin factory
Plugin* create_gamma_plugin() {
    return new GammaPlugin();
}

} // namespace havel
