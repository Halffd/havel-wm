// Window Destroy on Close Plugin Implementation

#include "DestroyOnClosePlugin.hpp"
#include <wm/View.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <Logger.h>
#include <cmath>
#include <random>

namespace havel {

void DestroyOnClosePlugin::init(CompositorAPI* api) {
    m_api = api;
    
    m_defaultEffect = DestroyEffect::FadeOut;
    m_defaultDuration = 0.3f;  // 300ms
    m_particleCount = 50;
    
    m_particleVBO = 0;
    m_particleShader = 0;
    
    LOG_INFO("[DestroyOnClose] Initialized (effect=%d, duration=%.2fs)", 
             static_cast<int>(m_defaultEffect), m_defaultDuration);
}

void DestroyOnClosePlugin::fini() {
    m_pendingDestroys.clear();
    LOG_INFO("[DestroyOnClose] Finalized");
}

bool DestroyOnClosePlugin::handleViewClose(View* view) {
    // Start animation instead of immediate close
    startDestroyAnimation(view);
    return true;  // Consume event (we'll remove view after animation)
}

bool DestroyOnClosePlugin::handleViewRemoved(View* view) {
    // Cancel any pending animation for this view
    m_pendingDestroys.erase(
        std::remove_if(m_pendingDestroys.begin(), m_pendingDestroys.end(),
            [view](const PendingDestroy& p) { return p.viewId == view->id(); }),
        m_pendingDestroys.end());
    return false;
}

bool DestroyOnClosePlugin::handleFrame() {
    // Calculate delta time
    static auto lastTime = std::chrono::steady_clock::now();
    auto currentTime = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;
    
    // Cap dt to prevent huge jumps
    if (dt > 0.1f) dt = 0.1f;
    
    updateAnimations(dt);
    
    return false;
}

void DestroyOnClosePlugin::startDestroyAnimation(View* view) {
    if (!view) return;
    
    PendingDestroy destroy;
    destroy.viewId = view->id();
    destroy.effect = m_defaultEffect;
    destroy.progress = 0;
    destroy.duration = m_defaultDuration;
    destroy.startGeom = view->geom();
    destroy.active = true;
    
    // Create effect-specific data
    switch (m_defaultEffect) {
        case DestroyEffect::Explode:
            createExplosion(&destroy, destroy.startGeom);
            break;
        case DestroyEffect::Shatter:
            createShatter(&destroy, destroy.startGeom);
            break;
        case DestroyEffect::Dissolve:
            createDissolve(&destroy, destroy.startGeom);
            break;
        default:
            break;
    }
    
    m_pendingDestroys.push_back(destroy);
    
    LOG_DEBUG("[DestroyOnClose] Started %d animation for view %u", 
              static_cast<int>(m_defaultEffect), view->id());
}

void DestroyOnClosePlugin::updateAnimations(float dt) {
    for (auto it = m_pendingDestroys.begin(); it != m_pendingDestroys.end(); ) {
        PendingDestroy& destroy = *it;
        
        if (!destroy.active) {
            it = m_pendingDestroys.erase(it);
            continue;
        }
        
        // Update progress
        destroy.progress += dt / destroy.duration;
        
        if (destroy.progress >= 1.0f) {
            // Animation complete - actually close the view
            destroy.progress = 1.0f;
            destroy.active = false;
            
            View* view = m_api->getViewById(destroy.viewId);
            if (view) {
                m_api->closeView(view);
            }
            
            LOG_DEBUG("[DestroyOnClose] Animation complete for view %u", destroy.viewId);
            ++it;
            continue;
        }
        
        // Update effect-specific state
        switch (destroy.effect) {
            case DestroyEffect::Explode:
            case DestroyEffect::Shatter:
                // Update particles
                for (auto& p : destroy.particles) {
                    p.x += p.vx * dt;
                    p.y += p.vy * dt;
                    p.vy += 200.0f * dt;  // Gravity
                    p.life -= dt / destroy.duration;
                }
                break;
                
            case DestroyEffect::Dissolve:
                // Particles fade randomly
                for (auto& p : destroy.particles) {
                    if (p.life > 0 && static_cast<float>(rand()) / RAND_MAX < dt * 2) {
                        p.life -= dt / destroy.duration;
                    }
                }
                break;
                
            default:
                break;
        }
        
        ++it;
    }
}

void DestroyOnClosePlugin::createExplosion(PendingDestroy* destroy, Rect geom) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> posDist(0, 1);
    std::uniform_real_distribution<float> velDist(-300, 300);
    std::uniform_real_distribution<float> sizeDist(2, 8);
    
    for (int i = 0; i < m_particleCount; i++) {
        DestroyParticle p;
        p.x = geom.x + geom.w * posDist(gen);
        p.y = geom.y + geom.h * posDist(gen);
        p.vx = velDist(gen);
        p.vy = velDist(gen) - 200;  // Initial upward bias
        p.life = 1.0f;
        p.size = sizeDist(gen);
        p.r = p.g = p.b = 0.8f;
        p.a = 1.0f;
        destroy->particles.push_back(p);
    }
}

void DestroyOnClosePlugin::createShatter(PendingDestroy* destroy, Rect geom) {
    // Create shard-like particles
    int shards = m_particleCount / 2;
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> posDist(0, 1);
    std::uniform_real_distribution<float> velDist(-100, 100);
    
    for (int i = 0; i < shards; i++) {
        DestroyParticle p;
        p.x = geom.x + geom.w * posDist(gen);
        p.y = geom.y + geom.h * posDist(gen);
        p.vx = velDist(gen);
        p.vy = velDist(gen);
        p.life = 1.0f;
        p.size = 10 + posDist(gen) * 20;
        p.r = p.g = p.b = 0.6f;
        p.a = 1.0f;
        destroy->particles.push_back(p);
    }
}

void DestroyOnClosePlugin::createDissolve(PendingDestroy* destroy, Rect geom) {
    // Create grid of pixels that will fade
    int gridSize = static_cast<int>(sqrt(m_particleCount));
    
    float cellW = geom.w / gridSize;
    float cellH = geom.h / gridSize;
    
    for (int row = 0; row < gridSize; row++) {
        for (int col = 0; col < gridSize; col++) {
            DestroyParticle p;
            p.x = geom.x + col * cellW + cellW / 2;
            p.y = geom.y + row * cellH + cellH / 2;
            p.vx = p.vy = 0;
            p.life = 1.0f;
            p.size = std::max(cellW, cellH);
            p.r = p.g = p.b = 0.5f;
            p.a = 1.0f;
            destroy->particles.push_back(p);
        }
    }
}

void DestroyOnClosePlugin::renderParticles() {
    // Would render particles using OpenGL
    // For each pending destroy with particles, draw them
}

void DestroyOnClosePlugin::setEffect(DestroyEffect effect) {
    m_defaultEffect = effect;
    LOG_INFO("[DestroyOnClose] Effect set to %d", static_cast<int>(effect));
}

void DestroyOnClosePlugin::setDuration(float duration) {
    m_defaultDuration = std::max(0.1f, std::min(2.0f, duration));
}

void DestroyOnClosePlugin::setParticleCount(int count) {
    m_particleCount = std::max(10, std::min(500, count));
}

} // namespace havel

// Plugin factory
extern "C" {
    havel::Plugin* create_destroy_on_close_plugin() {
        return new havel::DestroyOnClosePlugin();
    }
    
    void destroy_destroy_on_close_plugin(havel::Plugin* plugin) {
        delete plugin;
    }
}
