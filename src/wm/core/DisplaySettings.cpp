// DisplaySettings implementation (extracted from Server, phase 3).
// Applies gamma/temperature/brightness through the C bridge so all
// outputs update; effect flags are state-only for now.

#include <wm/DisplaySettings.hpp>
#include <wm/bridge.h>
#include <Logger.h>

namespace havel {

void DisplaySettings::setGamma(float gamma) {
    m_gamma = gamma;
    LOG_INFO("Gamma set to %.2f", gamma);
    apply();
}

void DisplaySettings::setTemperature(int kelvin) {
    m_temperature = kelvin;
    LOG_INFO("Temperature set to %dK", kelvin);
    apply();
}

void DisplaySettings::setBrightness(float brightness) {
    m_brightness = brightness;
    LOG_INFO("Brightness set to %.2f", brightness);
    apply();
}

void DisplaySettings::apply() {
    if (!m_applyHandle) return;
    havel_wlr_set_gamma(m_applyHandle, m_gamma);
    havel_wlr_set_temperature(m_applyHandle, m_temperature);
    havel_wlr_set_brightness(m_applyHandle, m_brightness);
}

} // namespace havel
