// Input Handler - Manages keyboards and pointers
// Pure C implementation for performance

#include "InputHandler.h"
#include "../BackendTypes.h"
#include <Logger.h>
#include <stdlib.h>
#include <xkbcommon/xkbcommon.h>

// ============================================================================
// Keyboard Handlers
// ============================================================================

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
    havel_keyboard_t *keyboard = wl_container_of(listener, keyboard, modifiers);
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->keyboard->modifiers);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
    havel_keyboard_t *keyboard = wl_container_of(listener, keyboard, key);
    struct havel_wlr_server *server = keyboard->server;
    struct wlr_keyboard_key_event *event = data;
    
    wlr_seat_set_keyboard(server->seat, keyboard->keyboard);
    
    // Update XKB state
    if (keyboard->xkb_state) {
        xkb_state_update_key(keyboard->xkb_state, event->keycode + 8,
                            event->state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);
    }
    
    // Forward to seat - keybindings handled at higher level
    wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
    havel_keyboard_t *keyboard = wl_container_of(listener, keyboard, destroy);
    
    // Free XKB resources
    if (keyboard->xkb_state) xkb_state_unref(keyboard->xkb_state);
    if (keyboard->keymap) xkb_keymap_unref(keyboard->keymap);
    
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    
    LOG_INFO("[INPUT] Keyboard destroyed");
    free(keyboard);
}

// ============================================================================
// Pointer Handlers
// ============================================================================

static void pointer_handle_motion(struct wl_listener *listener, void *data) {
    havel_pointer_t *pointer = wl_container_of(listener, pointer, motion);
    struct wlr_pointer_motion_event *event = data;
    
    wlr_cursor_move(pointer->server->cursor, &pointer->pointer->base,
                    event->delta_x, event->delta_y);
}

static void pointer_handle_motion_absolute(struct wl_listener *listener, void *data) {
    havel_pointer_t *pointer = wl_container_of(listener, pointer, motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    
    wlr_cursor_warp_absolute(pointer->server->cursor, &pointer->pointer->base,
                             event->x, event->y);
}

static void pointer_handle_button(struct wl_listener *listener, void *data) {
    havel_pointer_t *pointer = wl_container_of(listener, pointer, button);
    struct wlr_pointer_button_event *event = data;
    
    wlr_seat_pointer_notify_button(pointer->server->seat,
                                    event->time_msec, event->button, event->state);
}

static void pointer_handle_axis(struct wl_listener *listener, void *data) {
    havel_pointer_t *pointer = wl_container_of(listener, pointer, axis);
    struct wlr_pointer_axis_event *event = data;
    
    wlr_seat_pointer_notify_axis(pointer->server->seat, event->time_msec,
                                  event->orientation, event->delta,
                                  event->delta_discrete, event->source,
                                  event->relative_direction);
}

static void pointer_handle_frame(struct wl_listener *listener, void *data) {
    havel_pointer_t *pointer = wl_container_of(listener, pointer, frame);
    wlr_seat_pointer_notify_frame(pointer->server->seat);
}

static void pointer_handle_destroy(struct wl_listener *listener, void *data) {
    havel_pointer_t *pointer = wl_container_of(listener, pointer, destroy);
    
    wl_list_remove(&pointer->motion.link);
    wl_list_remove(&pointer->motion_absolute.link);
    wl_list_remove(&pointer->button.link);
    wl_list_remove(&pointer->axis.link);
    wl_list_remove(&pointer->frame.link);
    wl_list_remove(&pointer->destroy.link);
    wl_list_remove(&pointer->link);
    
    LOG_INFO("[INPUT] Pointer destroyed");
    free(pointer);
}

// ============================================================================
// Keyboard Creation
// ============================================================================

havel_keyboard_t* havel_keyboard_create(struct havel_wlr_server *server, struct wlr_input_device *device) {
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);
    
    LOG_INFO("[INPUT] Keyboard added");
    
    // Allocate keyboard structure
    havel_keyboard_t *keyboard = calloc(1, sizeof(havel_keyboard_t));
    if (!keyboard) {
        LOG_ERROR("[INPUT] Failed to allocate keyboard");
        return NULL;
    }
    
    keyboard->server = server;
    keyboard->keyboard = wlr_keyboard;
    
    // Setup XKB keymap
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    
    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    
    keyboard->keymap = wlr_keyboard->keymap;
    keyboard->xkb_state = xkb_state_new(keyboard->keymap);
    
    // Setup listeners
    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
    
    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
    
    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);
    
    // Add to keyboard list
    wl_list_insert(server->keyboards.prev, &keyboard->link);
    
    // Set keyboard to seat
    wlr_seat_set_keyboard(server->seat, wlr_keyboard);
    
    return keyboard;
}

void havel_keyboard_destroy(havel_keyboard_t *keyboard) {
    // Handler will be called by wlroots
}

// ============================================================================
// Pointer Creation
// ============================================================================

havel_pointer_t* havel_pointer_create(struct havel_wlr_server *server, struct wlr_input_device *device) {
    struct wlr_pointer *wlr_pointer = wlr_pointer_from_input_device(device);
    
    LOG_INFO("[INPUT] Pointer added");
    
    // Allocate pointer structure
    havel_pointer_t *pointer = calloc(1, sizeof(havel_pointer_t));
    if (!pointer) {
        LOG_ERROR("[INPUT] Failed to allocate pointer");
        return NULL;
    }
    
    pointer->server = server;
    pointer->pointer = wlr_pointer;
    
    // Setup listeners
    pointer->motion.notify = pointer_handle_motion;
    wl_signal_add(&wlr_pointer->events.motion, &pointer->motion);
    
    pointer->motion_absolute.notify = pointer_handle_motion_absolute;
    wl_signal_add(&wlr_pointer->events.motion_absolute, &pointer->motion_absolute);
    
    pointer->button.notify = pointer_handle_button;
    wl_signal_add(&wlr_pointer->events.button, &pointer->button);
    
    pointer->axis.notify = pointer_handle_axis;
    wl_signal_add(&wlr_pointer->events.axis, &pointer->axis);
    
    pointer->frame.notify = pointer_handle_frame;
    wl_signal_add(&wlr_pointer->events.frame, &pointer->frame);
    
    pointer->destroy.notify = pointer_handle_destroy;
    wl_signal_add(&device->events.destroy, &pointer->destroy);
    
    // Add to pointer list
    wl_list_insert(server->pointers.prev, &pointer->link);
    
    return pointer;
}

void havel_pointer_destroy(havel_pointer_t *pointer) {
    // Handler will be called by wlroots
}

// ============================================================================
// Input Manager Initialization
// ============================================================================

void havel_input_init(struct havel_wlr_server *server) {
    wl_list_init(&server->keyboards);
    wl_list_init(&server->pointers);
    LOG_INFO("[INPUT] Input manager initialized");
}
