// Wobbly Windows Plugin Implementation

#include "WobblyWindowsPlugin.hpp"
#include <wm/View.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <Logger.h>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace havel {

void WobblyWindowsPlugin::init(CompositorAPI* api) {
    m_api = api;
    
    // Default physics parameters
    m_stiffness = 0.3f;     // Spring stiffness
    m_damping = 0.95f;      // Velocity damping
    m_mass = 1.0f;          // Vertex mass
    m_gridResolution = 10;  // Grid subdivisions
    
    m_dragging = false;
    m_dragViewId = 0;
    m_lastX = m_lastY = 0;
    m_lastTime = 0;
    
    m_vbo = 0;
    m_shader = 0;
    
    LOG_INFO("[WobblyWindows] Initialized (stiffness=%.2f, damping=%.2f)", 
             m_stiffness, m_damping);
}

void WobblyWindowsPlugin::fini() {
    m_meshes.clear();
    LOG_INFO("[WobblyWindows] Finalized");
}

bool WobblyWindowsPlugin::handleViewAdded(View* view) {
    createMesh(view);
    return false;  // Don't consume event
}

bool WobblyWindowsPlugin::handleViewRemoved(View* view) {
    destroyMesh(view->id());
    return false;
}

bool WobblyWindowsPlugin::handlePointerButton(uint32_t button, bool pressed, double x, double y) {
    if (button == 0x110) {  // Left button
        if (pressed) {
            m_dragging = true;
            m_lastX = x;
            m_lastY = y;
            m_lastTime = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        } else {
            m_dragging = false;
            m_dragViewId = 0;
        }
    }
    return false;
}

bool WobblyWindowsPlugin::handlePointerMotion(double x, double y) {
    if (!m_dragging) return false;
    
    double currentTime = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    float dt = currentTime - m_lastTime;
    if (dt < 0.001f) dt = 0.001f;  // Cap at 1ms
    
    // Calculate movement delta
    float dx = x - m_lastX;
    float dy = y - m_lastY;
    
    // Apply force to all meshes
    for (auto& mesh : m_meshes) {
        applyForce(&mesh, dx * 100.0f, dy * 100.0f);
    }
    
    m_lastX = x;
    m_lastY = y;
    m_lastTime = currentTime;
    
    updatePhysics();
    
    return false;
}

bool WobblyWindowsPlugin::handleViewMove(View* view, int x, int y) {
    // Update mesh position
    WobblyMesh* mesh = getMesh(view);
    if (mesh) {
        for (auto& v : mesh->vertices) {
            if (!v.fixed) {
                v.ox = v.x;
                v.oy = v.y;
            }
        }
    }
    return false;
}

bool WobblyWindowsPlugin::handleViewResize(View* view, int w, int h) {
    // Recreate mesh with new size
    destroyMesh(view->id());
    createMesh(view);
    return false;
}

void WobblyWindowsPlugin::createMesh(View* view) {
    if (!view) return;
    
    WobblyMesh mesh;
    mesh.viewId = view->id();
    mesh.rows = m_gridResolution;
    mesh.cols = m_gridResolution;
    mesh.springK = m_stiffness;
    mesh.damping = m_damping;
    mesh.mass = m_mass;
    
    Rect geom = view->geom();
    float cellW = geom.w / (float)(mesh.cols - 1);
    float cellH = geom.h / (float)(mesh.rows - 1);
    
    // Create grid of vertices
    for (int row = 0; row < mesh.rows; row++) {
        for (int col = 0; col < mesh.cols; col++) {
            WobblyVertex v;
            v.x = v.ox = geom.x + col * cellW;
            v.y = v.oy = geom.y + row * cellH;
            v.vx = v.vy = 0;
            v.fx = v.fy = 0;
            
            // Fix corner vertices
            v.fixed = ((row == 0 || row == mesh.rows - 1) && 
                      (col == 0 || col == mesh.cols - 1));
            
            mesh.vertices.push_back(v);
        }
    }
    
    m_meshes.push_back(mesh);
    
    LOG_DEBUG("[WobblyWindows] Created mesh for view %u (%dx%d grid)", 
              view->id(), mesh.cols, mesh.rows);
}

void WobblyWindowsPlugin::destroyMesh(uint32_t viewId) {
    m_meshes.erase(
        std::remove_if(m_meshes.begin(), m_meshes.end(),
            [viewId](const WobblyMesh& m) { return m.viewId == viewId; }),
        m_meshes.end());
}

WobblyMesh* WobblyWindowsPlugin::getMesh(View* view) {
    if (!view) return nullptr;
    
    for (auto& mesh : m_meshes) {
        if (mesh.viewId == view->id()) {
            return &mesh;
        }
    }
    return nullptr;
}

void WobblyWindowsPlugin::applyForce(WobblyMesh* mesh, float fx, float fy) {
    if (!mesh) return;
    
    // Apply force to non-fixed vertices
    for (auto& v : mesh->vertices) {
        if (!v.fixed) {
            v.fx += fx / mesh->mass;
            v.fy += fy / mesh->mass;
        }
    }
}

void WobblyWindowsPlugin::updatePhysics() {
    float dt = 0.016f;  // Assume 60fps

    for (auto& mesh : m_meshes) {
        Rect geom;
        View* view = m_api->getViewById(mesh.viewId);
        if (view) {
            geom = view->geom();
        } else {
            continue;
        }

        float cellW = geom.w / (float)(mesh.cols - 1);
        float cellH = geom.h / (float)(mesh.rows - 1);

        // Update each vertex
        for (int i = 0; i < mesh.vertices.size(); i++) {
            WobblyVertex& v = mesh.vertices[i];
            
            if (v.fixed) {
                // Fixed vertices stay at their target position
                int row = i / mesh.cols;
                int col = i % mesh.cols;
                v.x = v.ox = geom.x + col * cellW;
                v.y = v.oy = geom.y + row * cellH;
                v.vx = v.vy = 0;
                v.fx = v.fy = 0;
                continue;
            }
            
            // Spring force towards original position
            float sx = (v.ox - v.x) * mesh.springK;
            float sy = (v.oy - v.y) * mesh.springK;
            
            // Neighbor spring forces (connect to adjacent vertices)
            int row = i / mesh.cols;
            int col = i % mesh.cols;
            
            // Left neighbor
            if (col > 0) {
                WobblyVertex& left = mesh.vertices[i - 1];
                float dx = (left.x + cellW) - v.x;
                float dy = left.y - v.y;
                sx += dx * mesh.springK * 0.5f;
                sy += dy * mesh.springK * 0.5f;
            }
            
            // Right neighbor
            if (col < mesh.cols - 1) {
                WobblyVertex& right = mesh.vertices[i + 1];
                float dx = (right.x - cellW) - v.x;
                float dy = right.y - v.y;
                sx += dx * mesh.springK * 0.5f;
                sy += dy * mesh.springK * 0.5f;
            }
            
            // Top neighbor
            if (row > 0) {
                WobblyVertex& top = mesh.vertices[i - mesh.cols];
                float dx = top.x - v.x;
                float dy = (top.y + cellH) - v.y;
                sx += dx * mesh.springK * 0.5f;
                sy += dy * mesh.springK * 0.5f;
            }
            
            // Bottom neighbor
            if (row < mesh.rows - 1) {
                WobblyVertex& bottom = mesh.vertices[i + mesh.cols];
                float dx = bottom.x - v.x;
                float dy = (bottom.y - cellH) - v.y;
                sx += dx * mesh.springK * 0.5f;
                sy += dy * mesh.springK * 0.5f;
            }
            
            // Apply forces
            v.fx += sx;
            v.fy += sy;
            
            // Integrate
            integrateVertex(v, dt);
        }
    }
}

void WobblyWindowsPlugin::integrateVertex(WobblyVertex& v, float dt) {
    // Semi-implicit Euler integration
    float ax = v.fx / m_mass;
    float ay = v.fy / m_mass;
    
    v.vx += ax * dt;
    v.vy += ay * dt;
    
    // Apply damping
    v.vx *= m_damping;
    v.vy *= m_damping;
    
    // Update position
    v.x += v.vx * dt;
    v.y += v.vy * dt;
    
    // Reset forces
    v.fx = v.fy = 0;
}

void WobblyWindowsPlugin::setStiffness(float stiffness) {
    m_stiffness = std::max(0.01f, std::min(1.0f, stiffness));
    for (auto& mesh : m_meshes) {
        mesh.springK = m_stiffness;
    }
}

void WobblyWindowsPlugin::setDamping(float damping) {
    m_damping = std::max(0.5f, std::min(0.99f, damping));
    for (auto& mesh : m_meshes) {
        mesh.damping = m_damping;
    }
}

void WobblyWindowsPlugin::setMass(float mass) {
    m_mass = std::max(0.1f, std::min(10.0f, mass));
    for (auto& mesh : m_meshes) {
        mesh.mass = m_mass;
    }
}

void WobblyWindowsPlugin::setGridResolution(int resolution) {
    m_gridResolution = std::max(3, std::min(30, resolution));
    // Would recreate all meshes with new resolution
}

void WobblyWindowsPlugin::renderOverlay() {
    // Would render debug visualization of the mesh
    // For production, would use the deformed mesh for window rendering
}

} // namespace havel

// Plugin factory
extern "C" {
    havel::Plugin* create_wobbly_windows_plugin() {
        return new havel::WobblyWindowsPlugin();
    }
    
    void destroy_wobbly_windows_plugin(havel::Plugin* plugin) {
        delete plugin;
    }
}
