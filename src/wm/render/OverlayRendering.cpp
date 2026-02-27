// C++ implementation of overlay rendering for C render pipeline

#include <wm/render/OverlayRenderer.hpp>
#include <wm/plugins/PluginManager.hpp>
#include <cstdio>

// Access C pipeline struct - defined in render_c_wrappers.c
typedef struct havel_render_pipeline havel_render_pipeline_t;

struct havel_render_pipeline {
    void* output;
    void* renderer;
    float zoom;
    float gamma;
    float brightness;
    bool effectsEnabled;
    void* overlayRenderer;  // Opaque pointer to C++ OverlayRenderer
};

extern "C" {

void havel_render_pipeline_draw_overlays(havel_render_pipeline_t* pipeline, int width, int height, void* pluginManagerPtr) {
    if (!pipeline || !pluginManagerPtr || !pipeline->overlayRenderer) return;
    
    havel::OverlayRenderer* renderer = static_cast<havel::OverlayRenderer*>(pipeline->overlayRenderer);
    havel::PluginManager* pluginManager = static_cast<havel::PluginManager*>(pluginManagerPtr);
    
    // Begin overlay rendering
    renderer->beginFrame(width, height);
    
    // Let all plugins render their overlays
    pluginManager->renderOverlays(renderer);
    
    // End overlay rendering
    renderer->endFrame();
}

} // extern "C"
