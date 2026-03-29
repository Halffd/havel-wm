// Cursor Handler - Manages cursor movement and buttons
// Pure C implementation for performance

#include "Cursor.h"
#include "../BackendTypes.h"
#include <wlr/types/wlr_data_device.h>
#include <Logger.h>
#include <stdlib.h>

// ============================================================================
// Cursor Handlers
// ============================================================================

static void cursor_motion(struct wl_listener *listener, void *data) {
    havel_wlr_server_t *server = wl_container_of(listener, server, new_input);
    struct wlr_pointer_motion_event *event = data;
    
    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
}

static void cursor_motion_absolute(struct wl_listener *listener, void *data) {
    havel_wlr_server_t *server = wl_container_of(listener, server, new_input);
    struct wlr_pointer_motion_absolute_event *event = data;
    
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
}

static void cursor_button(struct wl_listener *listener, void *data) {
    havel_wlr_server_t *server = wl_container_of(listener, server, new_input);
    struct wlr_pointer_button_event *event = data;
    
    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

static void cursor_axis(struct wl_listener *listener, void *data) {
    havel_wlr_server_t *server = wl_container_of(listener, server, new_input);
    struct wlr_pointer_axis_event *event = data;
    
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation,
                                  event->delta, event->delta_discrete, event->source,
                                  event->relative_direction);
}

static void cursor_frame(struct wl_listener *listener, void *data) {
    havel_wlr_server_t *server = wl_container_of(listener, server, new_input);
    wlr_seat_pointer_notify_frame(server->seat);
}

// ============================================================================
// Cursor Initialization
// ============================================================================

void havel_cursor_init(havel_wlr_server_t *server) {
    // Create cursor
    server->cursor = wlr_cursor_create();
    if (!server->cursor) {
        LOG_ERROR("[CURSOR] Failed to create cursor");
        return;
    }
    
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
    
    // Create cursor manager for theme support
    server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    if (!server->cursor_mgr) {
        LOG_ERROR("[CURSOR] Failed to create cursor manager");
        wlr_cursor_destroy(server->cursor);
        return;
    }
    
    // Setup cursor event handlers
    server->new_input.notify = cursor_motion;  // Reuse for all cursor events
    wl_signal_add(&server->cursor->events.motion, &server->new_input);
    
    LOG_INFO("[CURSOR] Cursor initialized");
}

void havel_cursor_destroy(havel_wlr_server_t *server) {
    if (server->cursor_mgr) {
        wlr_xcursor_manager_destroy(server->cursor_mgr);
        server->cursor_mgr = NULL;
    }
    
    if (server->cursor) {
        wlr_cursor_destroy(server->cursor);
        server->cursor = NULL;
    }
    
    LOG_INFO("[CURSOR] Cursor destroyed");
}

// ============================================================================
// Seat Initialization
// ============================================================================

static void seat_request_cursor(struct wl_listener *listener, void *data) {
    havel_wlr_server_t *server = wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    
    struct wlr_seat_client *focused = server->seat->pointer_state.focused_client;
    
    // Only allow the focused client to set the cursor
    if (focused == event->seat_client) {
        wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
    }
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
    havel_wlr_server_t *server = wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    
    // wlroots 0.20: use wlr_seat_set_selection with proper API
    wlr_seat_pointer_notify_clear_focus(server->seat);
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

void havel_seat_init(havel_wlr_server_t *server) {
    // Create seat
    server->seat = wlr_seat_create(server->display, "seat0");
    if (!server->seat) {
        LOG_ERROR("[SEAT] Failed to create seat");
        return;
    }
    
    // Setup seat event handlers
    server->request_cursor.notify = seat_request_cursor;
    wl_signal_add(&server->seat->events.request_set_cursor, &server->request_cursor);
    
    server->request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&server->seat->events.request_set_selection, &server->request_set_selection);
    
    LOG_INFO("[SEAT] Seat initialized: %s", server->seat->name);
}

void havel_seat_destroy(havel_wlr_server_t *server) {
    if (server->seat) {
        wlr_seat_destroy(server->seat);
        server->seat = NULL;
        LOG_INFO("[SEAT] Seat destroyed");
    }
}
