// Wobbly Windows Plugin - Jiggle effect when moving windows

#pragma once

#include <wm/plugins/Plugin.hpp>
#include <wm/render/ShaderEffect.hpp>
#include <vector>
#include <chrono>

namespace havel {

/**
 * Wobbly window physics vertex
 */
struct WobblyVertex {
    float x, y;           // Position
    float ox, oy;         // Original position
    float vx, vy;         // Velocity
    float fx, fy;         // Force
    bool fixed;           // Is this vertex fixed (corners)
};

/**
 * Wobbly window mesh
 */
struct WobblyMesh {
    uint32_t viewId;
    std::vector<WobblyVertex> vertices;
    int rows;
    int cols;
    float springK;        // Spring constant
    float damping;        // Damping factor
    float mass;           // Vertex mass
};

/**
 * Wobbly Windows Plugin
 * 
 * Creates a jelly-like wobble effect when windows are moved.
 * Uses spring-mass-damper physics simulation.
 */
class WobblyWindowsPlugin : public Plugin {
public:
    const char* name() const override { return "wobbly_windows"; }
    const char* description() const override { 
        return "Wobbly jelly-like window movement effect"; 
    }
    const char* version() const override { return "1.0.0"; }
    
    void init(CompositorAPI* api) override;
    void fini() override;
    
    // Events
    void onMouseMotion(int x, int y) override;
    void onMouseButton(uint32_t button, bool pressed, int x, int y) override;
    void onViewMap(const ViewEvent& event) override;
    void onViewUnmap(const ViewEvent& event) override;
    void onViewDestroy(const ViewEvent& event) override;
    void onOutputFrame(const OutputFrameEvent& event) override;
    
    // Configuration
    void setStiffness(float stiffness);
    void setDamping(float damping);
    void setMass(float mass);
    void setGridResolution(int resolution);
    
    float stiffness() const { return m_stiffness; }
    float damping() const { return m_damping; }
    float mass() const { return m_mass; }
    int gridResolution() const { return m_gridResolution; }
    
private:
    void updatePhysics();
    void createMesh(View* view);
    void destroyMesh(uint32_t viewId);
    WobblyMesh* getMesh(View* view);
    void applyForce(WobblyMesh* mesh, float fx, float fy);
    void integrateVertex(WobblyVertex& v, float dt);
    void renderOverlay();
    
    CompositorAPI* m_api;
    std::vector<WobblyMesh> m_meshes;
    
    // Configuration
    float m_stiffness;
    float m_damping;
    float m_mass;
    int m_gridResolution;
    
    // State
    bool m_dragging;
    uint32_t m_dragViewId;
    double m_lastX, m_lastY;
    double m_lastTime;
    
    // Rendering
    GLuint m_vbo;
    GLuint m_shader;
};

} // namespace havel
