// Window Spring on Spawn Plugin - Bounce animation when windows open

#pragma once

#include <wm/plugins/Plugin.hpp>
#include <wm/Types.hpp>
#include <wm/View.hpp>
#include <vector>
#include <chrono>

namespace havel {

/**
 * Spring animation types
 */
enum class SpringEffect {
    Bounce,         // Overshoot and bounce back
    Elastic,        // Elastic stretch effect
    Pop,            // Quick scale pop
    Slide,          // Slide in with spring
    Fade,           // Fade in with spring
    Zoom,           // Zoom from center
};

/**
 * Pending spawn animation
 */
struct PendingSpawn {
    uint32_t viewId;
    SpringEffect effect;
    float progress;     // 0-1
    float duration;     // Total duration
    Rect targetGeom;    // Target geometry
    Rect currentGeom;   // Current animated geometry
    float scale;        // Current scale
    float opacity;      // Current opacity
    bool active;
    bool visible;       // Is view currently visible
};

/**
 * Window Spring on Spawn Plugin
 * 
 * Plays spring/bounce animations when windows are opened.
 * Supports multiple effect types.
 */
class SpringOnSpawnPlugin : public Plugin {
public:
    const char* name() const override { return "spring_on_spawn"; }
    const char* description() const override { 
        return "Spring/bounce animation effects on window spawn"; 
    }
    const char* version() const override { return "1.0.0"; }
    
    void init(CompositorAPI* api) override;
    void fini() override;
    
    // Events
    bool handleViewAdded(View* view) override;
    bool handleViewRemoved(View* view) override;
    bool handleFrame() override;
    
    // Configuration
    void setEffect(SpringEffect effect);
    void setDuration(float duration);
    void setBounciness(float bounciness);
    void setStiffness(float stiffness);
    
    SpringEffect effect() const { return m_defaultEffect; }
    float duration() const { return m_defaultDuration; }
    float bounciness() const { return m_bounciness; }
    float stiffness() const { return m_stiffness; }
    
private:
    void startSpawnAnimation(View* view);
    void updateAnimations(float dt);
    float easeOutBounce(float t);
    float easeOutElastic(float t);
    float easeOutBack(float t);
    
    CompositorAPI* m_api;
    std::vector<PendingSpawn> m_pendingSpawns;
    
    // Configuration
    SpringEffect m_defaultEffect;
    float m_defaultDuration;
    float m_bounciness;   // 0-1, how much to overshoot
    float m_stiffness;    // Spring stiffness
    
    // State
    bool m_hideViewsDuringAnimation;
};

} // namespace havel
