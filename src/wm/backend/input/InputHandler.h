// Input Handler - Manages keyboards and pointers
// Pure C implementation for performance

#pragma once

#include <wayland-server-core.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct havel_wlr_server;

// Keyboard handle
typedef struct havel_keyboard {
    struct havel_wlr_server *server;
    struct wlr_keyboard *keyboard;
    
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
    
    struct wl_list link;  // havel_wlr_server.keyboards
    
    // XKB state
    struct xkb_state *xkb_state;
    struct xkb_keymap *keymap;
} havel_keyboard_t;

// Pointer handle
typedef struct havel_pointer {
    struct havel_wlr_server *server;
    struct wlr_pointer *pointer;
    
    struct wl_listener motion;
    struct wl_listener motion_absolute;
    struct wl_listener button;
    struct wl_listener axis;
    struct wl_listener frame;
    struct wl_listener destroy;
    
    struct wl_list link;  // havel_wlr_server.pointers
} havel_pointer_t;

// Initialize input manager
void havel_input_init(struct havel_wlr_server *server);

// Create keyboard from input device
havel_keyboard_t* havel_keyboard_create(struct havel_wlr_server *server, struct wlr_input_device *device);

// Create pointer from input device
havel_pointer_t* havel_pointer_create(struct havel_wlr_server *server, struct wlr_input_device *device);

// Destroy keyboard
void havel_keyboard_destroy(havel_keyboard_t *keyboard);

// Destroy pointer
void havel_pointer_destroy(havel_pointer_t *pointer);

#ifdef __cplusplus
}
#endif
