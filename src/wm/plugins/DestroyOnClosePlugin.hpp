// Window Destroy on Close Plugin - Animation effects when closing windows

#pragma once

#include <wm/plugins/Plugin.hpp>
#include <wm/Types.hpp>
#include <wm/View.hpp>
#include <vector>
#include <chrono>

namespace havel {

/**
 * Destroy animation types
 */
enum class DestroyEffect {
    FadeOut,        // Fade to transparent
    Shrink,         // Shrink to center
    Explode,        // Explode into particles
    Shatter,        // Shatter into pieces
    Burn,           // Burn away effect
    Dissolve,       // Pixel dissolve
    SlideOut,       // Slide off screen
    ScaleDown,      // Scale down and fade
};

/**
 * Particle for explosion effects
 */
struct DestroyParticle {
    float x, y;         // Position
    float vx, vy;       // Velocity
    float life;         // Remaining life (0-1)
    float size;         // Particle size
    float r, g, b, a;   // Color
};

/**
 * Pending destroy animation
 */
struct PendingDestroy {
    uint32_t viewId;
    DestroyEffect effect;
    float progress;     // 0-1
    float duration;     // Total duration
    std::vector<DestroyParticle> particles;
    Rect startGeom;     // Starting geometry
    bool active;
};

/**
 * Window Destroy on Close Plugin
 * 
 * Plays animations when windows are closed.
 * Supports multiple effect types.
 */
class DestroyOnClosePlugin : public Plugin {
public:
    const char* name() const override { return "destroy_on_close"; }
    const char* description() const override { 
        return "Animated window destruction effects on close"; 
    }
    const char* version() const override { return "1.0.0"; }
    
    void init(CompositorAPI* api) override;
    void fini() override;
    
    // Events
    bool handleViewClose(View* view) override;
    bool handleViewRemoved(View* view) override;
    bool handleFrame() override;
    
    // Configuration
    void setEffect(DestroyEffect effect);
    void setDuration(float duration);
    void setParticleCount(int count);
    
    DestroyEffect effect() const { return m_defaultEffect; }
    float duration() const { return m_defaultDuration; }
    int particleCount() const { return m_particleCount; }
    
private:
    void startDestroyAnimation(View* view);
    void updateAnimations(float dt);
    void renderParticles();
    void createExplosion(PendingDestroy* destroy, Rect geom);
    void createShatter(PendingDestroy* destroy, Rect geom);
    void createDissolve(PendingDestroy* destroy, Rect geom);
    
    CompositorAPI* m_api;
    std::vector<PendingDestroy> m_pendingDestroys;
    
    // Configuration
    DestroyEffect m_defaultEffect;
    float m_defaultDuration;
    int m_particleCount;
    
    // Rendering
    GLuint m_particleVBO;
    GLuint m_particleShader;
};

} // namespace havel
