#include <wm/bridge.h>
#include <wm/Server.hpp>
#include <wm/plugins/Plugin.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include "WindowGroupManager.hpp"
#include "../desktop/DesktopManager.hpp"
#include <Logger.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <cstdint>
#include <unistd.h>
#include <string>
#include <cstdlib>

// Forward declare C server type and functions
typedef struct havel_wlr_server havel_wlr_server_t;

// Forward declare wlroots types
struct wlr_output;

extern "C" {
    void havel_wlr_set_gamma(havel_wlr_server_t* server, float gamma);
    void havel_wlr_set_temperature(havel_wlr_server_t* server, int kelvin);
    void havel_wlr_set_brightness(havel_wlr_server_t* server, float brightness);
    void havel_wlr_set_zoom_for_output(havel_wlr_server_t* server, int output_index, float zoom,
                                        float cursor_x, float cursor_y);
    void havel_wlr_set_zoom_for_output_simple(havel_wlr_server_t* server, int output_index, float zoom);
}

extern "C" {

struct havel_cpp_server {
    havel::Server* server;
    void* nativeHandle;  // havel_wlr_server_t* - for calling C layer functions
};

struct havel_cpp_server* havel_cpp_server_create(void) {
    auto* cpp = new havel_cpp_server;
    cpp->server = new havel::Server();
    cpp->nativeHandle = nullptr;
    
    // Start IPC server for external tool communication
    // HAVEL_IPC_SOCKET overrides the default name so a second compositor
    // instance gets its own IPC (with the live-socket check in
    // IPCServer::start, the default path would refuse a second instance
    // instead of stealing the first one's socket).
    const char* ipc_override = getenv("HAVEL_IPC_SOCKET");
    const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
    std::string socket_path;
    if (ipc_override && *ipc_override) {
        socket_path = ipc_override;
    } else if (runtime_dir) {
        socket_path = std::string(runtime_dir) + "/havel-wm.sock";
    } else {
        // Fallback to /tmp
        socket_path = "/tmp/havel-wm.sock";
    }

    cpp->server->startIPCServer(socket_path);
    
    return cpp;
}

void havel_cpp_server_destroy(struct havel_cpp_server* server) {
    if (!server) return;

    // Delete C-layer objects that were created with new
    if (server->server) {
        // TextInputManager: created in havel_cpp_server_init_text_input,
        // stored typed; Server does not own it.
        if (server->server->textInputManager()) {
            delete server->server->textInputManager();
        }

        // WindowGroupManager / DesktopManager are process-wide singletons
        // (getWindowGroupManager()/getDesktopManager()); their destroy
        // helpers delete AND null the globals so late accessor calls
        // lazily recreate instead of dangling.
        havel::destroyWindowGroupManager();
        havel::destroyDesktopManager();

        server->server->stopIPCServer();
        delete server->server;
    }
    delete server;
}

void havel_cpp_server_set_native_handle(struct havel_cpp_server* server, void* handle) {
    if (!server) return;
    server->nativeHandle = handle;
    server->server->setNativeHandle(handle);
}

void havel_cpp_server_set_overlay_layer(struct havel_cpp_server* server, void* overlay_layer) {
    if (!server) return;
    server->server->setOverlayLayer(overlay_layer);
}

void havel_cpp_server_init_text_input(struct havel_cpp_server* server, struct wl_display* display) {
    if (!server || !display) return;
    
    // Create TextInputManager and register with Server
    auto* textInputManager = new havel::TextInputManager(display);
    server->server->setTextInputManager(textInputManager);
    
    printf("[TextInput] IME support initialized\n");
}

void havel_cpp_process_ipc_events(struct havel_cpp_server* server) {
    if (!server) return;
    server->server->processIPCEvents();
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

void havel_cpp_on_view_title_changed(struct havel_cpp_server* server, void* c_view, const char* title) {
    if (!server || !c_view || !title) return;
    auto* view = static_cast<havel::View*>(c_view);
    server->server->onViewTitleChanged(view, std::string(title));
}

void havel_cpp_on_view_destroyed(struct havel_cpp_server* server, void* c_view) {
    if (!server || !c_view) return;
    auto* view = static_cast<havel::View*>(c_view);
    server->server->onViewDestroyed(view);
}

bool havel_cpp_on_key(struct havel_cpp_server* server, uint32_t keycode, bool pressed, uint32_t modifiers, uint32_t keysym, char key_char, const char* utf8) {
    if (!server) return false;
    return server->server->handleKey(keycode, pressed, modifiers, keysym, key_char, utf8);
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

void havel_cpp_update_animations(struct havel_cpp_server* server) {
    if (!server) return;
    server->server->updateAnimations();
}

void havel_cpp_dispatch_output_frame(struct havel_cpp_server* server, void* output, void* sceneOutput, int width, int height, int refresh) {
    if (!server || !output) return;

    havel::OutputFrameEvent frameEvent;
    frameEvent.output = output;
    frameEvent.sceneOutput = sceneOutput;
    frameEvent.width = width;
    frameEvent.height = height;
    frameEvent.refresh = refresh;

    server->server->pluginManager().dispatchOutputFrame(frameEvent);
}

void havel_cpp_get_background_color(struct havel_cpp_server* server, float* r, float* g, float* b) {
    if (!server) return;
    server->server->getBackgroundColor(r, g, b);
}

void havel_cpp_draw_overlays(struct havel_cpp_server* server, int width, int height) {
    if (!server || !server->server) return;

    // Get plugin manager and render overlays with the overlay renderer
    auto& pluginManager = server->server->pluginManager();
    havel::OverlayRenderer* renderer = server->server->getOverlayRenderer();
    
    if (!renderer) {
        // Overlay renderer not initialized - this is expected if setOverlayLayer wasn't called
        // or if OverlayRenderer::initialize() failed
        static int logCount = 0;
        if (logCount++ < 5) {
            printf("[Overlays] SKIPPED - renderer is NULL (call %d)\n", logCount);
        }
        return;
    }
    
    static int frameCount = 0;
    if (++frameCount % 60 == 0) {
        printf("[Overlays] Rendering %zu plugins\n", pluginManager.plugins().size());
    }
    
    pluginManager.renderOverlays(renderer);

    (void)width;
    (void)height;
}

void havel_cpp_server_spawn(struct havel_cpp_server* server, const char* command) {
    if (!server || !command) return;

    // Parse command into argv (simple space-splitting, no shell)
    char* cmd_copy = strdup(command);
    if (!cmd_copy) {
        LOG_ERROR("[Spawn] strdup failed");
        return;
    }

    char* argv[64];
    int argc = 0;
    char* token = strtok(cmd_copy, " \t\n");
    while (token && argc < 63) {
        argv[argc++] = token;
        token = strtok(NULL, " \t\n");
    }
    argv[argc] = NULL;

    if (argc == 0) {
        free(cmd_copy);
        return;
    }

    // Fork and exec the command
    pid_t pid = fork();
    if (pid == 0) {
        // Child process - execute command directly, no shell
        execvp(argv[0], argv);
        _exit(1);  // execvp failed
    } else if (pid > 0) {
        LOG_INFO("[Spawn] Launched command: %s (PID: %d)", command, pid);
    } else {
        LOG_ERROR("[Spawn] Failed to fork for command: %s", command);
    }
    free(cmd_copy);
}

void havel_cpp_init_window_groups(struct havel_cpp_server* server) {
    if (!server || !server->server) return;

    // Use the process-wide singleton so every consumer (current and
    // future) sees the same configured instance. Previously this
    // created a second instance that nothing ever read back.
    auto& groupManager = havel::getWindowGroupManager();

    server->server->setWindowGroupManager(&groupManager);

    // Add default window rules
    havel::WindowRule firefoxRule;
    firefoxRule.appId = "firefox";
    firefoxRule.groupType = havel::GroupType::Tabbed;
    groupManager.addWindowRule(firefoxRule);

    havel::WindowRule terminalRule;
    terminalRule.appId = "foot";
    terminalRule.groupType = havel::GroupType::SplitH;
    groupManager.addWindowRule(terminalRule);

    LOG_INFO("[Bridge] Window group manager initialized");
}

void havel_cpp_init_desktop(struct havel_cpp_server* server) {
    if (!server || !server->server) return;

    // Use the process-wide singleton so every consumer sees the same
    // configured instance. Previously this created a second instance
    // that nothing read back — NASAWallpaper's getDesktopManager()
    // lazily created a different, never-configured one instead.
    auto& desktopManager = havel::getDesktopManager();
    desktopManager.initialize();

    server->server->setDesktopManager(&desktopManager);

    // Bind logout shortcut (Ctrl+Alt+Delete)
    desktopManager.bindLogout(XKB_KEY_Delete, (1 << 0) | (1 << 2) | (1 << 3));  // Shift=bit0, Ctrl=bit2, Alt=bit3

    LOG_INFO("[Bridge] Desktop manager initialized");
}

void* havel_cpp_get_scene_graph(struct havel_cpp_server* server) {
    if (!server || !server->server) return nullptr;
    return server->server->sceneGraph();
}

void havel_cpp_process_desktop_mouse(struct havel_cpp_server* server, int button, bool pressed, int x, int y) {
    if (!server || !server->server) return;
    
    auto* desktopManager = static_cast<havel::DesktopManager*>(server->server->desktopManager());
    if (desktopManager) {
        desktopManager->processMouseButton(button, pressed, x, y);
    }
}

void havel_cpp_process_desktop_motion(struct havel_cpp_server* server, int x, int y) {
    if (!server || !server->server) return;
    
    auto* desktopManager = static_cast<havel::DesktopManager*>(server->server->desktopManager());
    if (desktopManager) {
        desktopManager->processMouseMove(x, y);
    }
}

void havel_cpp_process_desktop_key(struct havel_cpp_server* server, uint32_t keysym, uint32_t modifiers) {
    if (!server || !server->server) return;

    auto* desktopManager = static_cast<havel::DesktopManager*>(server->server->desktopManager());
    if (desktopManager) {
        desktopManager->processKeyDown(keysym, modifiers);
    }
}

} // extern "C"
