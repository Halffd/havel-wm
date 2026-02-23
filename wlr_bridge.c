#include <wm/wlr_bridge.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <wayland-server-core.h>

#define WLR_USE_UNSTABLE

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_shm.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define HAVE_WORKSPACES 1

#define HAVEL_WORKSPACE_COUNT 10

struct havel_wlr_server {
    struct wl_display *display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;

    struct wlr_compositor *compositor;
    struct wlr_output_layout *output_layout;
    struct wlr_scene *scene;

    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_xdg_surface;

    struct wlr_xwayland *xwayland;
    struct wl_listener new_xwayland_surface;

    struct wlr_seat *seat;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;

    struct wl_listener new_output;
    struct wl_listener new_input;

    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;

    struct havel_xdg_view *focused_xdg;

    struct wl_list xdg_views_mru; // havel_xdg_view::mru_link, front = most recent

    uint32_t active_workspace;
    struct wl_list outputs; // havel_output::link
};

struct havel_output {
    struct wlr_output *output;
    struct wlr_scene_output *scene_output;
    struct wl_listener frame;
    struct wl_listener destroy;

    struct havel_wlr_server *server;

    bool is_primary;
    struct wl_list link;

    struct wlr_scene_tree *workspaces[HAVEL_WORKSPACE_COUNT];
};

struct havel_keyboard {
    struct wlr_keyboard *keyboard;
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;

    struct havel_wlr_server *server;
};

struct havel_xdg_view {
    struct wlr_xdg_surface *xdg_surface;
    struct wlr_scene_tree *scene_tree;
    struct havel_wlr_server *server;

    struct wl_list mru_link;

    uint32_t workspace_id;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
};

struct havel_xwayland_view {
    struct wlr_xwayland_surface *xsurface;
    struct wlr_scene_tree *scene_tree;

    struct wl_listener destroy;
};

static void focus_xdg_view(struct havel_wlr_server *server, struct havel_xdg_view *view, struct wlr_surface *surface);
static void focus_mru_step(struct havel_wlr_server *server, bool backwards);

static void view_reparent_to_workspace(struct havel_xdg_view *view);
static void workspace_set_active(struct havel_wlr_server *server, uint32_t workspace_id);
static void workspace_step(struct havel_wlr_server *server, bool backwards);

static bool modifiers_have_alt_only(const struct wlr_keyboard_modifiers *mods) {
    if (!mods) {
        return false;
    }

    return (mods->depressed & WLR_MODIFIER_ALT) != 0;
}

static void view_reparent_to_workspace(struct havel_xdg_view *view) {
    if (!view || !view->server || !view->scene_tree) {
        return;
    }

    struct havel_wlr_server *server = view->server;
    struct havel_output *output = NULL;
    wl_list_for_each(output, &server->outputs, link) {
        struct wlr_scene_tree *dst = NULL;
        if (output->is_primary) {
            if (view->workspace_id < HAVEL_WORKSPACE_COUNT) {
                dst = output->workspaces[view->workspace_id];
            }
        } else {
            dst = output->workspaces[0];
        }

        if (dst) {
            wlr_scene_node_reparent(&view->scene_tree->node, dst);
        }
        break;
    }
}

static void workspace_set_active(struct havel_wlr_server *server, uint32_t workspace_id) {
    if (!server) {
        return;
    }

    if (workspace_id >= HAVEL_WORKSPACE_COUNT) {
        return;
    }

    server->active_workspace = workspace_id;

    struct havel_output *output = NULL;
    wl_list_for_each(output, &server->outputs, link) {
        if (!output->is_primary) {
            continue;
        }

        for (uint32_t i = 0; i < HAVEL_WORKSPACE_COUNT; ++i) {
            bool enable = (i == workspace_id);
            wlr_scene_node_set_enabled(&output->workspaces[i]->node, enable);
        }
    }
}

static void workspace_step(struct havel_wlr_server *server, bool backwards) {
    if (!server) {
        return;
    }

    uint32_t cur = server->active_workspace;
    uint32_t next = 0;
    if (backwards) {
        next = (cur + HAVEL_WORKSPACE_COUNT - 1) % HAVEL_WORKSPACE_COUNT;
    } else {
        next = (cur + 1) % HAVEL_WORKSPACE_COUNT;
    }
    workspace_set_active(server, next);
}

static bool modifiers_have_meta_only(const struct wlr_keyboard_modifiers *mods) {
    if (!mods) {
        return false;
    }

    return (mods->depressed & WLR_MODIFIER_LOGO) != 0;
}

static void spawn_foot(void) {
    pid_t pid = fork();
    if (pid < 0) {
        return;
    }

    if (pid == 0) {
        execlp("foot", "foot", (char *)NULL);
        _exit(127);
    }
}

static void focus_surface(struct havel_wlr_server *server, struct wlr_surface *surface) {
    if (!server || !surface) {
        return;
    }

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(server->seat, surface,
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
}

static void focus_mru_step(struct havel_wlr_server *server, bool backwards) {
    if (!server) {
        return;
    }

    if (wl_list_empty(&server->xdg_views_mru)) {
        return;
    }

    struct havel_xdg_view *target = NULL;

    if (!server->focused_xdg || wl_list_empty(&server->focused_xdg->mru_link)) {
        target = wl_container_of(server->xdg_views_mru.next, target, mru_link);
    } else if (backwards) {
        struct wl_list *next = server->focused_xdg->mru_link.next;
        if (next == &server->xdg_views_mru) {
            next = server->xdg_views_mru.next;
        }
        target = wl_container_of(next, target, mru_link);
    } else {
        struct wl_list *prev = server->focused_xdg->mru_link.prev;
        if (prev == &server->xdg_views_mru) {
            prev = server->xdg_views_mru.prev;
        }
        target = wl_container_of(prev, target, mru_link);
    }

    if (!target || !target->xdg_surface) {
        return;
    }

    focus_xdg_view(server, target, target->xdg_surface->surface);
}

static struct wlr_surface *seat_surface_at(struct havel_wlr_server *server, double lx, double ly, double *sx, double *sy) {
    struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (!node) {
        return NULL;
    }

    if (node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }

    struct wlr_scene_buffer *scene_buffer = wl_container_of(node, scene_buffer, node);
    struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return NULL;
    }

    return scene_surface->surface;
}

static struct havel_xdg_view *xdg_view_from_surface(struct wlr_surface *surface) {
    if (!surface) {
        return NULL;
    }

    struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_try_from_wlr_surface(surface);
    if (!xdg_surface) {
        return NULL;
    }

    return xdg_surface->data;
}

static void xdg_view_mru_promote(struct havel_xdg_view *view) {
    if (!view || !view->server) {
        return;
    }

    if (!wl_list_empty(&view->mru_link)) {
        wl_list_remove(&view->mru_link);
    }

    wl_list_insert(&view->server->xdg_views_mru, &view->mru_link);
}

static void focus_xdg_view(struct havel_wlr_server *server, struct havel_xdg_view *view, struct wlr_surface *surface) {
    if (!server || !surface) {
        return;
    }

    if (server->focused_xdg && server->focused_xdg != view) {
        struct wlr_xdg_toplevel *old_toplevel = server->focused_xdg->xdg_surface->toplevel;
        if (old_toplevel) {
            wlr_xdg_toplevel_set_activated(old_toplevel, false);
        }
    }

    server->focused_xdg = view;

    if (view && view->xdg_surface && view->xdg_surface->toplevel) {
        wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, true);
    }

    if (view && view->scene_tree) {
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
    }

    if (view) {
        xdg_view_mru_promote(view);
        view_reparent_to_workspace(view);
    }

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(server->seat, surface,
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
}

static void process_cursor_motion(struct havel_wlr_server *server, uint32_t time_msec) {
    double sx = 0.0, sy = 0.0;
    struct wlr_surface *surface = seat_surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);

    if (!surface) {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
        wlr_seat_pointer_clear_focus(server->seat);
        return;
    }

    wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;

    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);

    if (event->state != WL_POINTER_BUTTON_STATE_PRESSED) {
        return;
    }

    double sx = 0.0, sy = 0.0;
    struct wlr_surface *surface = seat_surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
    if (surface) {
        struct havel_xdg_view *view = xdg_view_from_surface(surface);
        focus_xdg_view(server, view, surface);
    }
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation,
        event->delta, event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
    struct havel_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->keyboard->modifiers);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct havel_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct havel_wlr_server *server = keyboard->server;
    struct wlr_keyboard_key_event *event = data;

    wlr_seat_set_keyboard(server->seat, keyboard->keyboard);

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        const uint32_t keycode = event->keycode + 8;
        const xkb_keysym_t *syms = NULL;
        int nsyms = xkb_state_key_get_syms(keyboard->keyboard->xkb_state, keycode, &syms);
        bool alt = modifiers_have_alt_only(&keyboard->keyboard->modifiers);
        bool meta = modifiers_have_meta_only(&keyboard->keyboard->modifiers);
        bool shift = (keyboard->keyboard->modifiers.depressed & WLR_MODIFIER_SHIFT) != 0;
        for (int i = 0; i < nsyms; ++i) {
            if (syms[i] == XKB_KEY_Tab) {
                if (alt) {
                    focus_mru_step(server, shift);
                    return;
                }

                if (meta) {
                    workspace_step(server, shift);
                    return;
                }
            }

            if (!alt) {
                continue;
            }

            if (syms[i] == XKB_KEY_Return) {
                spawn_foot();
                return;
            }

            if (syms[i] == XKB_KEY_q || syms[i] == XKB_KEY_Q) {
                wl_display_terminate(server->display);
                return;
            }
        }
    }

    wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
    struct havel_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    free(keyboard);
}

static void server_new_keyboard(struct havel_wlr_server *server, struct wlr_input_device *device) {
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    struct havel_keyboard *keyboard = calloc(1, sizeof(*keyboard));
    keyboard->server = server;
    keyboard->keyboard = wlr_keyboard;

    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);

    wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);

    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wlr_seat_set_keyboard(server->seat, wlr_keyboard);
}

static void server_new_pointer(struct havel_wlr_server *server, struct wlr_input_device *device) {
    wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        server_new_keyboard(server, device);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        server_new_pointer(server, device);
        break;
    default:
        break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (server->seat->keyboard_state.keyboard) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(server->seat, caps);
}

static void output_frame(struct wl_listener *listener, void *data) {
    struct havel_output *output = wl_container_of(listener, output, frame);

    const struct wlr_scene_output_state_options options = {
        .timer = NULL,
    };

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    if (!wlr_scene_output_build_state(output->scene_output, &state, &options)) {
        wlr_output_state_finish(&state);
        return;
    }

    if (!wlr_output_commit_state(output->output, &state)) {
        wlr_output_state_finish(&state);
        return;
    }

    wlr_output_state_finish(&state);
    wlr_scene_output_commit(output->scene_output, &options);
}

static void output_destroy(struct wl_listener *listener, void *data) {
    struct havel_output *output = wl_container_of(listener, output, destroy);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);
}

static void server_new_output(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    if (!wl_list_empty(&wlr_output->modes)) {
        struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_mode(&state, mode);
        wlr_output_state_set_enabled(&state, true);
        if (!wlr_output_commit_state(wlr_output, &state)) {
            wlr_output_state_finish(&state);
            return;
        }
        wlr_output_state_finish(&state);
    }

    struct havel_output *output = calloc(1, sizeof(*output));
    output->server = server;
    output->output = wlr_output;
    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);

    wl_list_insert(&server->outputs, &output->link);
    output->is_primary = (server->outputs.next == &output->link);

    for (uint32_t i = 0; i < HAVEL_WORKSPACE_COUNT; ++i) {
        output->workspaces[i] = wlr_scene_tree_create(&server->scene->tree);
        if (!output->is_primary) {
            wlr_scene_node_set_enabled(&output->workspaces[i]->node, i == 0);
        } else {
            wlr_scene_node_set_enabled(&output->workspaces[i]->node, i == server->active_workspace);
        }
    }

    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wlr_output_layout_add_auto(server->output_layout, wlr_output);
}

static void xdg_view_handle_map(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, map);
    (void)data;
    focus_xdg_view(view->server, view, view->xdg_surface->surface);
}

static void xdg_view_handle_unmap(struct wl_listener *listener, void *data) {
    (void)listener;
    (void)data;
}

static void xdg_view_handle_destroy(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, destroy);
    struct havel_wlr_server *server = view->server;
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);

    if (server && server->focused_xdg == view) {
        server->focused_xdg = NULL;
    }

    if (!wl_list_empty(&view->mru_link)) {
        wl_list_remove(&view->mru_link);
        wl_list_init(&view->mru_link);
    }
    free(view);
}

static void server_new_xdg_surface(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, new_xdg_surface);
    struct wlr_xdg_surface *xdg_surface = data;

    if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        return;
    }

    struct havel_xdg_view *view = calloc(1, sizeof(*view));
    view->server = server;
    view->xdg_surface = xdg_surface;
    xdg_surface->data = view;

    wl_list_init(&view->mru_link);
    view->workspace_id = server->active_workspace;

    struct havel_output *out = NULL;
    struct wlr_scene_tree *parent = &server->scene->tree;
    wl_list_for_each(out, &server->outputs, link) {
        if (out->is_primary && view->workspace_id < HAVEL_WORKSPACE_COUNT) {
            parent = out->workspaces[view->workspace_id];
        }
        break;
    }

    view->scene_tree = wlr_scene_tree_create(parent);
    wlr_scene_xdg_surface_create(view->scene_tree, xdg_surface);

    view->map.notify = xdg_view_handle_map;
    wl_signal_add(&xdg_surface->surface->events.map, &view->map);

    view->unmap.notify = xdg_view_handle_unmap;
    wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);

    view->destroy.notify = xdg_view_handle_destroy;
    wl_signal_add(&xdg_surface->events.destroy, &view->destroy);
}

static void xwayland_view_handle_destroy(struct wl_listener *listener, void *data) {
    struct havel_xwayland_view *view = wl_container_of(listener, view, destroy);
    wl_list_remove(&view->destroy.link);
    free(view);
}

static void server_new_xwayland_surface(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, new_xwayland_surface);
    struct wlr_xwayland_surface *xsurface = data;

    if (!xsurface->surface) {
        return;
    }

    struct havel_xwayland_view *view = calloc(1, sizeof(*view));
    view->xsurface = xsurface;
    xsurface->data = view;

    view->scene_tree = wlr_scene_tree_create(&server->scene->tree);
    wlr_scene_surface_create(view->scene_tree, xsurface->surface);

    view->destroy.notify = xwayland_view_handle_destroy;
    wl_signal_add(&xsurface->events.destroy, &view->destroy);
}

havel_wlr_server_t* havel_wlr_create(void) {
    wlr_log_init(WLR_DEBUG, NULL);

    struct havel_wlr_server *server = calloc(1, sizeof(*server));
    wl_list_init(&server->xdg_views_mru);
    wl_list_init(&server->outputs);
    server->active_workspace = 0;
    server->display = wl_display_create();
    if (!server->display) {
        free(server);
        return NULL;
    }

    server->backend = wlr_backend_autocreate(wl_display_get_event_loop(server->display), NULL);
    if (!server->backend) {
        wl_display_destroy(server->display);
        free(server);
        return NULL;
    }

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer) {
        wlr_backend_destroy(server->backend);
        wl_display_destroy(server->display);
        free(server);
        return NULL;
    }

    wlr_renderer_init_wl_display(server->renderer, server->display);

    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    server->compositor = wlr_compositor_create(server->display, 5, server->renderer);
    struct wlr_subcompositor *sub = wlr_subcompositor_create(server->display);
    fprintf(stderr, "subcompositor_create: %p\n", (void *)sub);
    if (!sub) {
        fprintf(stderr, "subcompositor_create failed\n");
        abort();
    }
    wlr_shm_create_with_renderer(server->display, 1, server->renderer);
    wlr_data_device_manager_create(server->display);

    server->output_layout = wlr_output_layout_create(server->display);
    server->scene = wlr_scene_create();

    server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
    server->new_xdg_surface.notify = server_new_xdg_surface;
    wl_signal_add(&server->xdg_shell->events.new_surface, &server->new_xdg_surface);

    server->xwayland = wlr_xwayland_create(server->display, server->compositor, true);
    if (server->xwayland) {
        server->new_xwayland_surface.notify = server_new_xwayland_surface;
        wl_signal_add(&server->xwayland->events.new_surface, &server->new_xwayland_surface);
        setenv("DISPLAY", server->xwayland->display_name, true);
    }

    server->new_output.notify = server_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);

    server->seat = wlr_seat_create(server->display, "seat0");

    server->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
    server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

    server->cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);

    server->cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);

    server->cursor_button.notify = server_cursor_button;
    wl_signal_add(&server->cursor->events.button, &server->cursor_button);

    server->cursor_axis.notify = server_cursor_axis;
    wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);

    server->cursor_frame.notify = server_cursor_frame;
    wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

    server->new_input.notify = server_new_input;
    wl_signal_add(&server->backend->events.new_input, &server->new_input);

    return server;
}

void havel_wlr_destroy(havel_wlr_server_t *server) {
    if (!server) {
        return;
    }

    if (server->display) {
        wl_display_destroy_clients(server->display);
    }

    if (server->backend) {
        wlr_backend_destroy(server->backend);
    }

    if (server->display) {
        wl_display_destroy(server->display);
    }

    free(server);
}

int havel_wlr_run(havel_wlr_server_t *server) {
    if (!server) {
        return 1;
    }

    const char *socket = wl_display_add_socket_auto(server->display);
    if (!socket) {
        return 1;
    }

    if (!wlr_backend_start(server->backend)) {
        return 1;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    printf("Havel Compositor running on %s\n", socket);

    wl_display_run(server->display);
    return 0;
}
