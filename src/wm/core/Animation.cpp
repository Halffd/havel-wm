#include <wm/Animation.hpp>
#include <cmath>
#include <chrono>

namespace havel {

// ============================================================================
// Easing Functions
// ============================================================================

float Easing::linear(float t) {
    return t;
}

float Easing::easeInOutQuad(float t) {
    return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

float Easing::easeInOutCubic(float t) {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

float Easing::easeInOutQuart(float t) {
    return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) / 2.0f;
}

float Easing::easeInOutQuint(float t) {
    return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) / 2.0f;
}

float Easing::easeInOutSine(float t) {
    return -(std::cos(M_PI * t) - 1.0f) / 2.0f;
}

float Easing::easeInOutExpo(float t) {
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f 
                    : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
}

float Easing::easeInOutBack(float t) {
    const float c1 = 1.70158f;
    const float c2 = c1 * 1.525f;
    return t < 0.5f 
        ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) / 2.0f
        : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) / 2.0f;
}

// ============================================================================
// Animation Presets
// ============================================================================

AnimationConfig AnimationPresets::fade() {
    AnimationConfig config;
    config.durationMs = 200;
    config.easing = Easing::easeInOutSine;
    config.enabled = true;
    return config;
}

AnimationConfig AnimationPresets::slide() {
    AnimationConfig config;
    config.durationMs = 300;
    config.easing = Easing::easeInOutQuart;
    config.enabled = true;
    return config;
}

AnimationConfig AnimationPresets::scale() {
    AnimationConfig config;
    config.durationMs = 150;
    config.easing = Easing::easeInOutBack;
    config.enabled = true;
    return config;
}

AnimationConfig AnimationPresets::move() {
    AnimationConfig config;
    config.durationMs = 200;
    config.easing = Easing::easeInOutQuad;
    config.enabled = true;
    return config;
}

AnimationConfig AnimationPresets::resize() {
    AnimationConfig config;
    config.durationMs = 200;
    config.easing = Easing::easeInOutQuad;
    config.enabled = true;
    return config;
}

AnimationConfig AnimationPresets::none() {
    AnimationConfig config;
    config.durationMs = 0;
    config.easing = Easing::linear;
    config.enabled = false;
    return config;
}

// ============================================================================
// Animation
// ============================================================================

Animation::Animation() = default;

Animation& Animation::setDuration(uint32_t ms) {
    m_config.durationMs = ms;
    return *this;
}

Animation& Animation::setEasing(std::function<float(float)> easing) {
    m_config.easing = easing;
    return *this;
}

Animation& Animation::setOnUpdate(UpdateCallback cb) {
    m_onUpdate = cb;
    return *this;
}

Animation& Animation::setOnComplete(CompleteCallback cb) {
    m_onComplete = cb;
    return *this;
}

void Animation::start() {
    if (!m_config.enabled || m_config.durationMs == 0) {
        // Instant completion
        m_progress = 1.0f;
        if (m_onUpdate) m_onUpdate(1.0f);
        m_state = AnimationState::Completed;
        if (m_onComplete) m_onComplete();
        return;
    }
    
    m_state = AnimationState::Running;
    m_progress = 0.0f;
    m_startTime = std::chrono::steady_clock::now();
}

void Animation::cancel() {
    m_state = AnimationState::Cancelled;
    m_progress = 0.0f;
}

void Animation::update() {
    if (m_state != AnimationState::Running) return;
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_startTime).count();
    
    if (elapsed >= static_cast<int64_t>(m_config.durationMs)) {
        m_progress = 1.0f;
        m_state = AnimationState::Completed;
        if (m_onUpdate) m_onUpdate(1.0f);
        if (m_onComplete) m_onComplete();
        return;
    }
    
    float t = static_cast<float>(elapsed) / static_cast<float>(m_config.durationMs);
    m_progress = m_config.easing(t);
    
    if (m_onUpdate) m_onUpdate(m_progress);
}

} // namespace havel
