#pragma once

// Display settings state extracted from Server (phase 3).
// Owns gamma/temperature/brightness/effect flags; Server keeps its public
// setters/getters as forwarders so plugins (GammaPlugin via CompositorAPI)
// and the C bridge see no API change.
// Hardware application happens through the C bridge handle supplied by
// Server (havel_wlr_set_*), passed per-call via setApplyHandle().

#include <cstdint>

struct havel_wlr_server;
typedef struct havel_wlr_server havel_wlr_server_t;

namespace havel {

class DisplaySettings {
public:
    // Settings application is routed through the C bridge; Server passes
    // its native handle here (once, after the bridge creates the server).
    void setApplyHandle(havel_wlr_server_t* handle) { m_applyHandle = handle; }

    void setGamma(float gamma);
    void setTemperature(int kelvin);
    void setBrightness(float brightness);

    void setGrayscaleEnabled(bool enabled) { m_grayscaleEnabled = enabled; }
    void setNegativeEnabled(bool enabled) { m_negativeEnabled = enabled; }

    float gamma() const { return m_gamma; }
    int temperature() const { return m_temperature; }
    float brightness() const { return m_brightness; }
    bool isGrayscaleEnabled() const { return m_grayscaleEnabled; }
    bool isNegativeEnabled() const { return m_negativeEnabled; }

    void toggleGrayscale() { m_grayscaleEnabled = !m_grayscaleEnabled; }
    void toggleNegative() { m_negativeEnabled = !m_negativeEnabled; }

private:
    // Push current gamma/temperature/brightness through the C bridge so
    // outputs update. Effect flags are state-only (no bridge entry yet).
    void apply();

    float m_gamma = 1.0f;
    int m_temperature = 6500;  // Kelvin
    float m_brightness = 1.0f;
    bool m_grayscaleEnabled = false;
    bool m_negativeEnabled = false;
    havel_wlr_server_t* m_applyHandle = nullptr;
};

} // namespace havel
