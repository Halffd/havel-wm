#pragma once

#include <cstdint>
#include <functional>
#include <chrono>

namespace havel {

/**
 * Easing functions for animations.
 * Based on https://easings.net/
 */
class Easing {
public:
    // Linear - no easing
    static float linear(float t);
    
    // Ease in/out quadratic
    static float easeInOutQuad(float t);
    
    // Ease in/out cubic
    static float easeInOutCubic(float t);
    
    // Ease in/out quartic
    static float easeInOutQuart(float t);
    
    // Ease in/out quintic
    static float easeInOutQuint(float t);
    
    // Ease in/out sine
    static float easeInOutSine(float t);
    
    // Ease in/out exponential
    static float easeInOutExpo(float t);
    
    // Ease in/out back (overshoot)
    static float easeInOutBack(float t);
};

/**
 * Animation timing and state.
 */
enum class AnimationState {
    NotStarted,
    Running,
    Completed,
    Cancelled
};

/**
 * Animation configuration.
 */
struct AnimationConfig {
    uint32_t durationMs = 250;          // Default 250ms
    std::function<float(float)> easing = Easing::easeInOutQuad;
    bool enabled = true;
};

/**
 * Animation presets.
 */
struct AnimationPresets {
    static AnimationConfig fade();           // Fade in/out
    static AnimationConfig slide();          // Slide transitions
    static AnimationConfig scale();          // Scale effect
    static AnimationConfig move();           // Move/reposition
    static AnimationConfig resize();         // Resize
    static AnimationConfig none();           // Instant (no animation)
};

/**
 * Running animation instance.
 */
class Animation {
public:
    using UpdateCallback = std::function<void(float progress)>;
    using CompleteCallback = std::function<void()>;

    Animation();
    
    // Configure animation
    Animation& setDuration(uint32_t ms);
    Animation& setEasing(std::function<float(float)> easing);
    Animation& setOnUpdate(UpdateCallback cb);
    Animation& setOnComplete(CompleteCallback cb);
    
    // Control
    void start();
    void cancel();
    void update();
    
    // State
    AnimationState state() const { return m_state; }
    float progress() const { return m_progress; }
    bool isRunning() const { return m_state == AnimationState::Running; }
    
private:
    AnimationConfig m_config;
    UpdateCallback m_onUpdate;
    CompleteCallback m_onComplete;
    
    AnimationState m_state = AnimationState::NotStarted;
    float m_progress = 0.0f;
    std::chrono::steady_clock::time_point m_startTime;
};

} // namespace havel
