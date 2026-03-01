#include <wm/bridge.h>
#include <wm/Server.hpp>
#include <wm/plugins/Plugin.hpp>
#include <cstdint>

// Forward declare C server type and functions
typedef struct havel_wlr_server havel_wlr_server_t;

extern "C" {
    void havel_wlr_set_gamma(havel_wlr_server_t* server, float gamma);
    void havel_wlr_set_temperature(havel_wlr_server_t* server, int kelvin);
    void havel_wlr_set_brightness(havel_wlr_server_t* server, float brightness);
}

// Global callback pointers (accessible from Server.cpp)
ViewSetPositionFn g_view_set_position = nullptr;
ViewSetSizeFn g_view_set_size = nullptr;
ViewFocusFn g_view_focus = nullptr;
ViewRaiseFn g_view_raise = nullptr;
ViewGetGeometryFn g_view_get_geometry = nullptr;
ViewCloseFn g_view_close = nullptr;
ViewSetFullscreenFn g_view_set_fullscreen = nullptr;
ViewMinimizeFn g_view_minimize = nullptr;
WorkspaceArrangeFn g_workspace_arrange = nullptr;
WorkspaceSetActiveFn g_workspace_set_active = nullptr;
ServerQuitFn g_server_quit = nullptr;
ServerSpawnFn g_server_spawn = nullptr;

extern "C" {

struct havel_cpp_server {
    havel::Server* server;
    void* nativeHandle;  // havel_wlr_server_t* - for calling C layer functions
};

struct havel_cpp_server* havel_cpp_server_create(void) {
    auto* cpp = new havel_cpp_server;
    cpp->server = new havel::Server();
    cpp->nativeHandle = nullptr;
    return cpp;
}

void havel_cpp_server_destroy(struct havel_cpp_server* server) {
    if (!server) return;
    delete server->server;
    delete server;
}

void* havel_cpp_server_get_native_handle(struct havel_cpp_server* server) {
    if (!server) return nullptr;
    return server->server->nativeHandle();
}

void havel_cpp_server_set_native_handle(struct havel_cpp_server* server, void* handle) {
    if (!server) return;
    server->nativeHandle = handle;
    server->server->setNativeHandle(handle);
}

void* havel_cpp_on_xdg_surface_new(struct havel_cpp_server* server, void* c_view, uint32_t workspace_id, const char* appId, const char* title) {
    if (!server || !c_view) return nullptr;
    
    // C++ creates and owns the View object
    // workspace_id is passed from C so C++ owns the truth
    auto* view = server->server->createXdgView(c_view, workspace_id, appId, title);
    
    // Return opaque pointer for C to store
    return static_cast<void*>(view);
}

void havel_cpp_on_view_mapped(struct havel_cpp_server* server, void* c_view) {
    if (!server || !c_view) return;
    auto* view = static_cast<havel::View*>(c_view);
    server->server->onViewMapped(view);
}

void havel_cpp_on_view_unmapped(struct havel_cpp_server* server, void* c_view) {
    if (!server || !c_view) return;
    auto* view = static_cast<havel::View*>(c_view);
    server->server->onViewUnmapped(view);
}

void havel_cpp_on_view_destroyed(struct havel_cpp_server* server, void* c_view) {
    if (!server || !c_view) return;
    auto* view = static_cast<havel::View*>(c_view);
    server->server->onViewDestroyed(view);
}

bool havel_cpp_on_key(struct havel_cpp_server* server, uint32_t keycode, bool pressed, uint32_t modifiers, uint32_t keysym, char key_char) {
    if (!server) return false;
    return server->server->handleKey(keycode, pressed, modifiers, keysym, key_char);
}

void havel_cpp_on_pointer_button(struct havel_cpp_server* server, uint32_t button, bool pressed, double x, double y) {
    if (!server) return;
    server->server->handlePointerButton(button, pressed, x, y);
}

void havel_cpp_on_pointer_motion(struct havel_cpp_server* server, double x, double y) {
    if (!server) return;
    server->server->handlePointerMotion(x, y);
}

void havel_cpp_on_pointer_decoration_motion(struct havel_cpp_server* server, int x, int y) {
    if (!server) return;
    server->server->pluginManager().onMouseMotion(x, y);
}

void havel_cpp_on_pointer_decoration_button(struct havel_cpp_server* server, uint32_t button, bool pressed, int x, int y) {
    if (!server) return;
    server->server->pluginManager().onMouseButton(button, pressed, x, y);
}

void havel_cpp_set_output_geometry(struct havel_cpp_server* server, uint32_t workspace_id, int x, int y, int w, int h) {
    if (!server) return;
    havel::Rect geom{x, y, w, h};
    server->server->setOutputGeometry(workspace_id, geom);
}

void havel_cpp_set_active_workspace(struct havel_cpp_server* server, uint32_t workspace_id) {
    if (!server) return;
    // Workspace switching is handled by Server::setActiveWorkspace
}

void havel_cpp_update_animations(struct havel_cpp_server* server) {
    if (!server) return;
    server->server->updateAnimations();
}

void havel_cpp_dispatch_output_frame(struct havel_cpp_server* server, void* output, void* sceneOutput) {
    if (!server) return;
    
    havel::OutputFrameEvent frameEvent;
    frameEvent.output = output;
    frameEvent.sceneOutput = sceneOutput;
    frameEvent.width = 1920;  // Would get from actual output
    frameEvent.height = 1080;
    frameEvent.refresh = 60000;  // 60Hz in mHz
    
    server->server->pluginManager().dispatchOutputFrame(frameEvent);
}

void havel_cpp_get_background_color(struct havel_cpp_server* server, float* r, float* g, float* b) {
    if (!server) return;
    server->server->getBackgroundColor(r, g, b);
}

void havel_cpp_set_gamma(struct havel_cpp_server* server, float gamma) {
    if (!server) return;
    server->server->setGamma(gamma);
    // Apply to all outputs via C layer
    if (server->nativeHandle) {
        havel_wlr_set_gamma((havel_wlr_server_t*)server->nativeHandle, gamma);
    }
}

void havel_cpp_set_temperature(struct havel_cpp_server* server, int kelvin) {
    if (!server) return;
    server->server->setTemperature(kelvin);
    // Apply to all outputs via C layer
    if (server->nativeHandle) {
        havel_wlr_set_temperature((havel_wlr_server_t*)server->nativeHandle, kelvin);
    }
}

void havel_cpp_set_brightness(struct havel_cpp_server* server, float brightness) {
    if (!server) return;
    server->server->setBrightness(brightness);
    // Apply to all outputs via C layer
    if (server->nativeHandle) {
        havel_wlr_set_brightness((havel_wlr_server_t*)server->nativeHandle, brightness);
    }
}

void havel_cpp_draw_overlays(struct havel_cpp_server* server, int width, int height) {
    if (!server || !server->server) return;
    (void)width;
    (void)height;
}

void* havel_cpp_get_plugin_manager(struct havel_cpp_server* server) {
    if (!server || !server->server) return nullptr;
    return &server->server->pluginManager();
}

} // extern "C"
