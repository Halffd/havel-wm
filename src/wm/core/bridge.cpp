#include <wm/bridge.h>
#include <wm/Server.hpp>
#include <wm/plugins/Plugin.hpp>
#include <cstdint>

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
};

struct havel_cpp_server* havel_cpp_server_create(void) {
    auto* cpp = new havel_cpp_server;
    cpp->server = new havel::Server();
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

void havel_cpp_on_xdg_surface_new(struct havel_cpp_server* server, void* xdg_surface) {
    if (!server || !xdg_surface) return;
    server->server->createXdgView(xdg_surface);
}

void havel_cpp_on_view_mapped(struct havel_cpp_server* server, void* view) {
    if (!server || !view) return;
    server->server->onViewMapped(static_cast<havel::View*>(view));
}

void havel_cpp_on_view_unmapped(struct havel_cpp_server* server, void* view) {
    if (!server || !view) return;
    server->server->onViewUnmapped(static_cast<havel::View*>(view));
}

void havel_cpp_on_view_destroyed(struct havel_cpp_server* server, void* view) {
    if (!server || !view) return;
    server->server->onViewDestroyed(static_cast<havel::View*>(view));
}

bool havel_cpp_on_key(struct havel_cpp_server* server, uint32_t keycode, bool pressed, uint32_t modifiers) {
    if (!server) return false;
    return server->server->handleKey(keycode, pressed, modifiers);
}

void havel_cpp_on_pointer_button(struct havel_cpp_server* server, uint32_t button, bool pressed, double x, double y) {
    if (!server) return;
    server->server->handlePointerButton(button, pressed, x, y);
}

void havel_cpp_on_pointer_motion(struct havel_cpp_server* server, double x, double y) {
    if (!server) return;
    server->server->handlePointerMotion(x, y);
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

} // extern "C"
