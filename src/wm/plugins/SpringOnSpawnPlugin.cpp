// Window Spring on Spawn Plugin Implementation

#include "SpringOnSpawnPlugin.hpp"
#include <wm/View.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <Logger.h>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace havel {

void SpringOnSpawnPlugin::init(CompositorAPI* api) {
    m_api = api;
    
    m_defaultEffect = SpringEffect::Bounce;
    m_defaultDuration = 0.4f;  // 400ms
    m_bounciness = 0.5f;       // Medium bounce
    m_stiffness = 0.7f;        // Medium stiffness
    
    m_hideViewsDuringAnimation = true;
    
    LOG_INFO("[SpringOnSpawn] Initialized (effect=%d, duration=%.2fs)", 
             static_cast<int>(m_defaultEffect), m_defaultDuration);
}

void SpringOnSpawnPlugin::fini() {
    m_pendingSpawns.clear();
    LOG_INFO("[SpringOnSpawn] Finalized");
}

bool SpringOnSpawnPlugin::handleViewAdded(View* view) {
    // Start animation for new view
    startSpawnAnimation(view);
    return true;  // Consume event (we'll show view after animation starts)
}

bool SpringOnSpawnPlugin::handleViewRemoved(View* view) {
    // Cancel any pending animation for this view
    m_pendingSpawns.erase(
        std::remove_if(m_pendingSpawns.begin(), m_pendingSpawns.end(),
            [view](const PendingSpawn& p) { return p.viewId == view->id(); }),
        m_pendingSpawns.end());
    return false;
}

bool SpringOnSpawnPlugin::handleFrame() {
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

void SpringOnSpawnPlugin::startSpawnAnimation(View* view) {
    if (!view) return;
    
    PendingSpawn spawn;
    spawn.viewId = view->id();
    spawn.effect = m_defaultEffect;
    spawn.progress = 0;
    spawn.duration = m_defaultDuration;
    spawn.targetGeom = view->geom();
    spawn.active = true;
    spawn.visible = false;
    
    // Initial state based on effect
    switch (m_defaultEffect) {
        case SpringEffect::Bounce:
        case SpringEffect::Pop:
        case SpringEffect::Zoom:
            // Start from center, scaled down
            spawn.currentGeom = spawn.targetGeom;
            spawn.currentGeom.x = spawn.targetGeom.x + spawn.targetGeom.w / 2;
            spawn.currentGeom.y = spawn.targetGeom.y + spawn.targetGeom.h / 2;
            spawn.currentGeom.w = 0;
            spawn.currentGeom.h = 0;
            spawn.scale = 0.0f;
            spawn.opacity = 0.0f;
            break;
            
        case SpringEffect::Slide:
            // Start from top
            spawn.currentGeom = spawn.targetGeom;
            spawn.currentGeom.y = spawn.targetGeom.y - spawn.targetGeom.h;
            spawn.scale = 1.0f;
            spawn.opacity = 0.0f;
            break;
            
        case SpringEffect::Fade:
            // Start at target position, invisible
            spawn.currentGeom = spawn.targetGeom;
            spawn.scale = 1.0f;
            spawn.opacity = 0.0f;
            break;
            
        case SpringEffect::Elastic:
            // Start from center, scaled down
            spawn.currentGeom = spawn.targetGeom;
            spawn.currentGeom.x = spawn.targetGeom.x + spawn.targetGeom.w / 2;
            spawn.currentGeom.y = spawn.targetGeom.y + spawn.targetGeom.h / 2;
            spawn.currentGeom.w = 0;
            spawn.currentGeom.h = 0;
            spawn.scale = 0.0f;
            spawn.opacity = 0.0f;
            break;
    }
    
    m_pendingSpawns.push_back(spawn);
    
    // Hide the view initially
    if (m_hideViewsDuringAnimation) {
        m_api->setViewOpacity(view, 0.0f);
    }
    
    LOG_DEBUG("[SpringOnSpawn] Started %d animation for view %u", 
              static_cast<int>(m_defaultEffect), view->id());
}

void SpringOnSpawnPlugin::updateAnimations(float dt) {
    for (auto it = m_pendingSpawns.begin(); it != m_pendingSpawns.end(); ) {
        PendingSpawn& spawn = *it;
        
        if (!spawn.active) {
            it = m_pendingSpawns.erase(it);
            continue;
        }
        
        // Update progress
        spawn.progress += dt / spawn.duration;
        
        if (spawn.progress >= 1.0f) {
            // Animation complete
            spawn.progress = 1.0f;
            spawn.active = false;
            spawn.visible = true;
            
            // Set final geometry and opacity
            View* view = m_api->getViewById(spawn.viewId);
            if (view) {
                m_api->setViewGeometry(view, 
                    spawn.targetGeom.x, spawn.targetGeom.y,
                    spawn.targetGeom.w, spawn.targetGeom.h);
                m_api->setViewOpacity(view, 1.0f);
            }
            
            LOG_DEBUG("[SpringOnSpawn] Animation complete for view %u", spawn.viewId);
            ++it;
            continue;
        }
        
        // Calculate interpolated values based on effect
        float t = spawn.progress;
        float easedT;
        
        switch (spawn.effect) {
            case SpringEffect::Bounce:
                easedT = easeOutBounce(t);
                break;
            case SpringEffect::Elastic:
                easedT = easeOutElastic(t);
                break;
            case SpringEffect::Pop:
                easedT = easeOutBack(t);
                break;
            default:
                easedT = t;
                break;
        }
        
        // Apply interpolation based on effect
        View* view = m_api->getViewById(spawn.viewId);
        if (!view) {
            ++it;
            continue;
        }
        
        switch (spawn.effect) {
            case SpringEffect::Bounce:
            case SpringEffect::Pop:
            case SpringEffect::Zoom:
                // Scale from center
                spawn.scale = easedT;
                spawn.opacity = std::min(1.0f, easedT * 2);  // Fade in faster
                
                spawn.currentGeom.w = spawn.targetGeom.w * spawn.scale;
                spawn.currentGeom.h = spawn.targetGeom.h * spawn.scale;
                spawn.currentGeom.x = spawn.targetGeom.x + (spawn.targetGeom.w - spawn.currentGeom.w) / 2;
                spawn.currentGeom.y = spawn.targetGeom.y + (spawn.targetGeom.h - spawn.currentGeom.h) / 2;
                
                m_api->setViewGeometry(view,
                    spawn.currentGeom.x, spawn.currentGeom.y,
                    spawn.currentGeom.w, spawn.currentGeom.h);
                m_api->setViewOpacity(view, spawn.opacity);
                break;
                
            case SpringEffect::Slide:
                // Slide from top
                float slideY = spawn.targetGeom.y - spawn.targetGeom.h * (1 - easedT);
                spawn.opacity = easedT;
                
                m_api->setViewGeometry(view,
                    spawn.targetGeom.x, slideY,
                    spawn.targetGeom.w, spawn.targetGeom.h);
                m_api->setViewOpacity(view, spawn.opacity);
                break;
                
            case SpringEffect::Fade:
                // Just fade in
                spawn.opacity = easedT;
                m_api->setViewOpacity(view, spawn.opacity);
                break;
                
            case SpringEffect::Elastic:
                // Elastic stretch effect
                spawn.scale = easedT;
                spawn.opacity = std::min(1.0f, easedT * 2);
                
                // Add some width oscillation for elastic effect
                float oscillation = sin(t * M_PI * 4) * (1 - t) * m_bounciness;
                spawn.currentGeom.w = spawn.targetGeom.w * spawn.scale * (1 + oscillation);
                spawn.currentGeom.h = spawn.targetGeom.h * spawn.scale;
                spawn.currentGeom.x = spawn.targetGeom.x + (spawn.targetGeom.w - spawn.currentGeom.w) / 2;
                spawn.currentGeom.y = spawn.targetGeom.y + (spawn.targetGeom.h - spawn.currentGeom.h) / 2;
                
                m_api->setViewGeometry(view,
                    spawn.currentGeom.x, spawn.currentGeom.y,
                    spawn.currentGeom.w, spawn.currentGeom.h);
                m_api->setViewOpacity(view, spawn.opacity);
                break;
        }
        
        ++it;
    }
}

float SpringOnSpawnPlugin::easeOutBounce(float t) {
    const float n1 = 7.5625f;
    const float d1 = 2.75f;
    
    if (t < 1.0f / d1) {
        return n1 * t * t;
    } else if (t < 2.0f / d1) {
        return n1 * (t -= 1.5f / d1) * t + 0.75f;
    } else if (t < 2.5f / d1) {
        return n1 * (t -= 2.25f / d1) * t + 0.9375f;
    } else {
        return n1 * (t -= 2.625f / d1) * t + 0.984375f;
    }
}

float SpringOnSpawnPlugin::easeOutElastic(float t) {
    const float c4 = (2 * M_PI) / 3;
    
    if (t == 0) return 0;
    if (t == 1) return 1;
    
    return pow(2, -10 * t) * sin((t * 10 - 0.75f) * c4) + 1;
}

float SpringOnSpawnPlugin::easeOutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1;
    
    return 1 + c3 * pow(t - 1, 3) + c1 * pow(t - 1, 2);
}

void SpringOnSpawnPlugin::setEffect(SpringEffect effect) {
    m_defaultEffect = effect;
    LOG_INFO("[SpringOnSpawn] Effect set to %d", static_cast<int>(effect));
}

void SpringOnSpawnPlugin::setDuration(float duration) {
    m_defaultDuration = std::max(0.1f, std::min(2.0f, duration));
}

void SpringOnSpawnPlugin::setBounciness(float bounciness) {
    m_bounciness = std::max(0.0f, std::min(1.0f, bounciness));
}

void SpringOnSpawnPlugin::setStiffness(float stiffness) {
    m_stiffness = std::max(0.1f, std::min(1.0f, stiffness));
}

} // namespace havel

// Plugin factory
extern "C" {
    havel::Plugin* create_spring_on_spawn_plugin() {
        return new havel::SpringOnSpawnPlugin();
    }
    
    void destroy_spring_on_spawn_plugin(havel::Plugin* plugin) {
        delete plugin;
    }
}
