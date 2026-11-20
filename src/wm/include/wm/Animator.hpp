#pragma once

#include <wm/Animation.hpp>
#include <vector>
#include <memory>
#include <functional>

namespace havel {

/**
 * Manages multiple concurrent animations.
 */
class Animator {
public:
    using AnimationPtr = std::shared_ptr<Animation>;

    Animator();
    ~Animator();

    // Create and track animations
    AnimationPtr create();
    AnimationPtr create(const AnimationConfig& config);
    
    // Animation factories for common operations
    AnimationPtr fade(float from, float to, std::function<void(float)> setter);
    AnimationPtr slide(int fromX, int fromY, int toX, int toY, 
                       std::function<void(int, int)> setter);
    AnimationPtr scale(float from, float to, std::function<void(float)> setter);
    AnimationPtr move(int fromX, int fromY, int toX, int toY,
                      std::function<void(int, int)> setter);
    AnimationPtr resize(int fromW, int fromH, int toW, int toH,
                        std::function<void(int, int)> setter);
    
    // Update all running animations
    void update();
    
    // Remove completed animations
    void cleanup();
    
    // Cancel all animations
    void cancelAll();
    
    // Enable/disable all animations
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    
    // Global speed multiplier (for testing/debug)
    void setSpeedMultiplier(float multiplier) { m_speedMultiplier = multiplier; }
    
private:
    std::vector<AnimationPtr> m_animations;
    bool m_enabled = true;
    float m_speedMultiplier = 1.0f;
};

} // namespace havel
