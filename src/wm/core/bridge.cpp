#include <wm/bridge.h>
#include <wm/Server.hpp>
#include <wm/plugins/Plugin.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include "../input/GestureRecognizer.hpp"
#include "WindowGroupManager.hpp"
#include "../input/ComboManager.hpp"
#include "../desktop/DesktopManager.hpp"
#include <Logger.h>
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
    const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
    std::string socket_path;
    if (runtime_dir) {
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
    server->server->stopIPCServer();
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

void havel_cpp_set_active_workspace(struct havel_cpp_server* server, uint32_t workspace_id) {
    if (!server) return;
    // Workspace switching is handled by Server::setActiveWorkspace
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

// Per-monitor control
void havel_cpp_set_gamma_for_output(struct havel_cpp_server* server, int output_index, float gamma) {
    if (!server) return;
    server->server->setGamma(gamma);  // Also update global for new outputs
    if (server->nativeHandle) {
        havel_wlr_set_gamma_for_output((havel_wlr_server_t*)server->nativeHandle, output_index, gamma);
    }
}

void havel_cpp_set_temperature_for_output(struct havel_cpp_server* server, int output_index, int kelvin) {
    if (!server) return;
    server->server->setTemperature(kelvin);
    if (server->nativeHandle) {
        havel_wlr_set_temperature_for_output((havel_wlr_server_t*)server->nativeHandle, output_index, kelvin);
    }
}

void havel_cpp_set_brightness_for_output(struct havel_cpp_server* server, int output_index, float brightness) {
    if (!server) return;
    server->server->setBrightness(brightness);
    if (server->nativeHandle) {
        havel_wlr_set_brightness_for_output((havel_wlr_server_t*)server->nativeHandle, output_index, brightness);
    }
}

// Per-monitor zoom with cursor-centered zoom
void havel_cpp_set_zoom_for_output(struct havel_cpp_server* server, int output_index, float zoom,
                                    double cursor_x, double cursor_y) {
    if (!server) return;
    if (server->nativeHandle) {
        havel_wlr_set_zoom_for_output((havel_wlr_server_t*)server->nativeHandle, 
                                       output_index, zoom, (float)cursor_x, (float)cursor_y);
    }
}

// Wrapper without cursor position
void havel_cpp_set_zoom_for_output_simple(struct havel_cpp_server* server, int output_index, float zoom) {
    if (!server) return;
    if (server->nativeHandle) {
        havel_wlr_set_zoom_for_output_simple((havel_wlr_server_t*)server->nativeHandle, output_index, zoom);
    }
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

void* havel_cpp_get_plugin_manager(struct havel_cpp_server* server) {
    if (!server || !server->server) return nullptr;
    return &server->server->pluginManager();
}

void havel_cpp_server_spawn(struct havel_cpp_server* server, const char* command) {
    if (!server || !command) return;

    // Fork and exec the command
    pid_t pid = fork();
    if (pid == 0) {
        // Child process - execute command through shell
        execl("/bin/sh", "sh", "-c", command, (char*)NULL);
        _exit(1);  // execl failed
    } else if (pid > 0) {
        LOG_INFO("[Spawn] Launched command: %s (PID: %d)", command, pid);
    } else {
        LOG_ERROR("[Spawn] Failed to fork for command: %s", command);
    }
}

void havel_cpp_alt_tab_select(struct havel_cpp_server* server, int index) {
    if (!server || !server->server) return;

    // Get all views and focus the selected one
    auto& pluginManager = server->server->pluginManager();
    auto views = pluginManager.getAllViews();

    if (index >= 0 && index < static_cast<int>(views.size())) {
        havel::View* selectedView = views[index];
        if (selectedView) {
            LOG_INFO("[AltTab] Focusing selected window: %s - %s",
                     selectedView->appId().c_str(),
                     selectedView->title().c_str());
            pluginManager.focusView(selectedView);
        }
    }
}

void havel_cpp_init_gestures(struct havel_cpp_server* server) {
    if (!server || !server->server) return;
    
    // Create gesture recognizer
    auto* recognizer = new havel::GestureRecognizer();
    recognizer->initialize(50.0f, 100.0f, 300);
    
    // Set up gesture callback
    recognizer->setGestureCallback([server](const havel::GestureResult& result) {
        LOG_INFO("[Gesture] Recognized: %s (confidence: %.2f)", 
                 result.name.c_str(), result.confidence);
        
        // Trigger gesture action via GestureManager
        havel::GestureManager::getInstance().triggerGesture(result);
    });
    
    // Set up shake callback
    recognizer->setShakeCallback([server](float intensity) {
        LOG_INFO("[Gesture] Shake detected! Intensity: %.2f", intensity);
        // Could trigger shake action here
    });
    
    // Set up combo callback
    recognizer->setComboCallback([server](int clickCount) {
        LOG_INFO("[Gesture] Combo: %d clicks", clickCount);
        // Could trigger combo action here
    });
    
    server->server->setGestureRecognizer(recognizer);
    
    // Initialize gesture manager with default mappings
    havel::GestureManager::getInstance().initialize();
    
    LOG_INFO("[Bridge] Gesture recognition initialized");
}

void havel_cpp_process_gesture_motion(struct havel_cpp_server* server, double x, double y, uint64_t timestamp) {
    if (!server || !server->server) return;
    
    auto* recognizer = static_cast<havel::GestureRecognizer*>(server->server->gestureRecognizer());
    if (recognizer && recognizer->isGesturesEnabled()) {
        recognizer->processMotion(static_cast<float>(x), static_cast<float>(y), timestamp);
    }
}

void havel_cpp_process_gesture_button(struct havel_cpp_server* server, int button, bool pressed, double x, double y, uint64_t timestamp) {
    if (!server || !server->server) return;
    
    auto* recognizer = static_cast<havel::GestureRecognizer*>(server->server->gestureRecognizer());
    if (recognizer && recognizer->isGesturesEnabled()) {
        recognizer->processButton(button, pressed, static_cast<float>(x), static_cast<float>(y), timestamp);
    }
}

void havel_cpp_init_window_groups(struct havel_cpp_server* server) {
    if (!server || !server->server) return;
    
    // Initialize window group manager
    auto* groupManager = new havel::WindowGroupManager();
    groupManager->initialize();
    
    server->server->setWindowGroupManager(groupManager);
    
    // Add default window rules
    havel::WindowRule firefoxRule;
    firefoxRule.appId = "firefox";
    firefoxRule.groupType = havel::GroupType::Tabbed;
    groupManager->addWindowRule(firefoxRule);
    
    havel::WindowRule terminalRule;
    terminalRule.appId = "foot";
    terminalRule.groupType = havel::GroupType::SplitH;
    groupManager->addWindowRule(terminalRule);
    
    LOG_INFO("[Bridge] Window group manager initialized");
}

void* havel_cpp_get_window_group_manager(struct havel_cpp_server* server) {
    if (!server || !server->server) return nullptr;
    return server->server->windowGroupManager();
}

void havel_cpp_process_combo_key(struct havel_cpp_server* server, uint32_t keycode, bool pressed, uint32_t modifiers) {
    if (!server || !server->server) return;
    
    // Process through combo manager
    auto& comboManager = havel::getComboManager();
    if (comboManager.isInitialized()) {
        comboManager.processKeyEvent(keycode, pressed, modifiers);
    }
}

void havel_cpp_init_desktop(struct havel_cpp_server* server) {
    if (!server || !server->server) return;
    
    // Initialize desktop manager
    auto* desktopManager = new havel::DesktopManager();
    desktopManager->initialize();
    
    server->server->setDesktopManager(desktopManager);
    
    // Bind logout shortcut (Ctrl+Alt+Delete)
    desktopManager->bindLogout(111, (1 << 1) | (1 << 2));  // Ctrl+Alt+Delete
    
    LOG_INFO("[Bridge] Desktop manager initialized");
}

void* havel_cpp_get_desktop_manager(struct havel_cpp_server* server) {
    if (!server || !server->server) return nullptr;
    return server->server->desktopManager();
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

void havel_cpp_process_desktop_key(struct havel_cpp_server* server, uint32_t keycode, uint32_t modifiers) {
    if (!server || !server->server) return;
    
    auto* desktopManager = static_cast<havel::DesktopManager*>(server->server->desktopManager());
    if (desktopManager) {
        desktopManager->processKeyDown(keycode, modifiers);
    }
}

} // extern "C"
