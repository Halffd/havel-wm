// Backend Server - Main wlroots server initialization
// Pure C implementation for performance

#pragma once

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_scene.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct havel_wlr_server;

// Create server
struct havel_wlr_server* havel_server_create(struct wl_display *display);

// Start server (initialize all subsystems)
void havel_server_start(struct havel_wlr_server *server);

// Destroy server
void havel_server_destroy(struct havel_wlr_server *server);

#ifdef __cplusplus
}
#endif
