// Cursor Handler - Manages cursor movement and buttons
// Pure C implementation for performance

#pragma once

#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_seat.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct havel_wlr_server;

// Initialize cursor
void havel_cursor_init(struct havel_wlr_server *server);

// Destroy cursor
void havel_cursor_destroy(struct havel_wlr_server *server);

// Initialize seat
void havel_seat_init(struct havel_wlr_server *server);

// Destroy seat
void havel_seat_destroy(struct havel_wlr_server *server);

#ifdef __cplusplus
}
#endif
