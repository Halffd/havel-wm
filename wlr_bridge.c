#include <wm/wlr_bridge.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

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

    struct wl_list xdg_views_ws[HAVEL_WORKSPACE_COUNT]; // havel_xdg_view::ws_link

    bool workspace_tiling_enabled[HAVEL_WORKSPACE_COUNT];

    uint32_t active_workspace;
    struct wl_list outputs; // havel_output::link

    struct havel_xdg_view *grabbed_xdg;
    struct havel_xwayland_view *grabbed_xwayland;
    uint32_t grab_button;
    double grab_cursor_x;
    double grab_cursor_y;
    int grab_view_x;
    int grab_view_y;
    int grab_view_w;
    int grab_view_h;
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

    struct wl_list ws_link;

    bool mapped;

    uint32_t workspace_id;

    int x;
    int y;

    int float_x;
    int float_y;
    int float_w;
    int float_h;
    bool have_float_geom;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;

    struct havel_xdg_view *parent;
    struct wl_list children;
    struct wl_list parent_link;
    struct wl_listener parent_destroy;
};

struct havel_xwayland_view {
    struct wlr_xwayland_surface *xsurface;
    struct wlr_scene_tree *scene_tree;

    struct wl_listener destroy;

    uint32_t workspace_id;
    int x;
    int y;

    int float_x;
    int float_y;
    int float_w;
    int float_h;
    bool have_float_geom;
};

static void focus_xdg_view(struct havel_wlr_server *server, struct havel_xdg_view *view, struct wlr_surface *surface);
static void focus_mru_step(struct havel_wlr_server *server, bool backwards);

static void arrange_workspace(struct havel_wlr_server *server, uint32_t workspace_id);
static void arrange_active_workspace(struct havel_wlr_server *server);
static struct havel_xdg_view *workspace_first_view(struct havel_wlr_server *server, uint32_t workspace_id);
static struct havel_xdg_view *workspace_next_view(struct havel_wlr_server *server, struct havel_xdg_view *cur, uint32_t workspace_id);
static void workspace_swap_with_next(struct havel_wlr_server *server, bool backwards);

static void workspace_toggle_tiling(struct havel_wlr_server *server);

static void view_reparent_to_workspace(struct havel_xdg_view *view);
static void workspace_set_active(struct havel_wlr_server *server, uint32_t workspace_id);
static void workspace_step(struct havel_wlr_server *server, bool backwards);

static void xdg_view_set_position(struct havel_xdg_view *view, int x, int y);
static void xwayland_view_set_position(struct havel_xwayland_view *view, int x, int y);

static struct havel_output *primary_output(struct havel_wlr_server *server);
static void output_get_box(struct havel_output *out, struct wlr_box *box);

static bool modifiers_have_alt_only(const struct wlr_keyboard_modifiers *mods) {
    if (!mods) {
        return false;
    }

    return (mods->depressed & WLR_MODIFIER_ALT) != 0;
}

static struct havel_output *output_at_cursor(struct havel_wlr_server *server) {
    if (!server) {
        return NULL;
    }

    if (server->focused_xdg && server->focused_xdg->mapped) {
        struct wlr_output *wlr_out = wlr_output_layout_output_at(server->output_layout,
            server->focused_xdg->x, server->focused_xdg->y);
        if (wlr_out) {
            struct havel_output *out = NULL;
            wl_list_for_each(out, &server->outputs, link) {
                if (out->output == wlr_out) {
                    return out;
                }
            }
        }
    }

    struct wlr_output *wlr_out = wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
    if (!wlr_out) {
        return primary_output(server);
    }

    struct havel_output *out = NULL;
    wl_list_for_each(out, &server->outputs, link) {
        if (out->output == wlr_out) {
            return out;
        }
    }
    return primary_output(server);
}

static void view_apply_default_floating_geom_xdg(struct havel_wlr_server *server, struct havel_xdg_view *view) {
    if (!server || !view || !view->xdg_surface || !view->xdg_surface->toplevel) {
        return;
    }

    if (view->have_float_geom) {
        xdg_view_set_position(view, view->float_x, view->float_y);
        if (view->float_w > 0 && view->float_h > 0) {
            wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, view->float_w, view->float_h);
        }
        return;
    }

    struct wlr_box geometry;
        geometry = view->xdg_surface->current.geometry;
    int width = geometry.width > 0 ? geometry.width : 800;
    int height = geometry.height > 0 ? geometry.height : 600;

    if (view->parent) {
        int x = view->parent->x + (view->parent->xdg_surface->current.geometry.width - width) / 2;
        int y = view->parent->y + (view->parent->xdg_surface->current.geometry.height - height) / 2;

        struct havel_output *out = output_at_cursor(server);
        struct wlr_box obox = {0};
        output_get_box(out, &obox);

        if (obox.width > 0 && obox.height > 0) {
            if (x < obox.x) x = obox.x;
            if (y < obox.y) y = obox.y;
            if (x + width > obox.x + obox.width) x = obox.x + obox.width - width;
            if (y + height > obox.y + obox.height) y = obox.y + obox.height - height;
        }

        xdg_view_set_position(view, x, y);
        wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, width, height);
        return;
    }

    struct havel_output *out = output_at_cursor(server);
    if (!out) {
        return;
    }
    struct wlr_box obox = {0};
    output_get_box(out, &obox);
    if (obox.width <= 0 || obox.height <= 0) {
        return;
    }

    int w = (obox.width * 60) / 100;
    int h = (obox.height * 60) / 100;
    int x = obox.x + (obox.width - w) / 2;
    int y = obox.y + (obox.height - h) / 2;
    if (w < 1) {
        w = 1;
    }
    if (h < 1) {
        h = 1;
    }

    xdg_view_set_position(view, x, y);
    wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, w, h);
}

static void view_apply_default_floating_geom_xwayland(struct havel_wlr_server *server, struct havel_xwayland_view *view) {
    if (!server || !view || !view->xsurface) {
        return;
    }

    if (view->have_float_geom) {
        wlr_xwayland_surface_configure(view->xsurface, view->float_x, view->float_y, view->float_w, view->float_h);
        xwayland_view_set_position(view, view->float_x, view->float_y);
        return;
    }

    struct havel_output *out = output_at_cursor(server);
    if (!out) {
        return;
    }
    struct wlr_box obox = {0};
    output_get_box(out, &obox);
    if (obox.width <= 0 || obox.height <= 0) {
        return;
    }

    int w = (obox.width * 60) / 100;
    int h = (obox.height * 60) / 100;
    int x = obox.x + (obox.width - w) / 2;
    int y = obox.y + (obox.height - h) / 2;
    if (w < 1) {
        w = 1;
    }
    if (h < 1) {
        h = 1;
    }

    wlr_xwayland_surface_configure(view->xsurface, x, y, w, h);
    xwayland_view_set_position(view, x, y);
}

static bool modifiers_have_alt_pressed(struct havel_wlr_server *server) {
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    if (!keyboard) {
        return false;
    }

    return (keyboard->modifiers.depressed & WLR_MODIFIER_ALT) != 0;
}

static bool modifiers_have_meta_pressed(struct havel_wlr_server *server) {
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    if (!keyboard) {
        return false;
    }

    return (keyboard->modifiers.depressed & WLR_MODIFIER_LOGO) != 0;
}

static void xdg_view_set_position(struct havel_xdg_view *view, int x, int y) {
    if (!view || !view->scene_tree) {
        return;
    }

    view->x = x;
    view->y = y;
    wlr_scene_node_set_position(&view->scene_tree->node, x, y);
}

static void xwayland_view_set_position(struct havel_xwayland_view *view, int x, int y) {
    if (!view || !view->scene_tree) {
        return;
    }

    view->x = x;
    view->y = y;
    wlr_scene_node_set_position(&view->scene_tree->node, x, y);
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

    arrange_active_workspace(server);
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

static struct havel_output *primary_output(struct havel_wlr_server *server) {
    if (!server) {
        return NULL;
    }

    struct havel_output *out = NULL;
    wl_list_for_each(out, &server->outputs, link) {
        if (out->is_primary) {
            return out;
        }
    }
    return NULL;
}

static void output_get_box(struct havel_output *out, struct wlr_box *box) {
    if (!out || !out->output || !box) {
        return;
    }

    struct wlr_box b = {0};
    wlr_output_layout_get_box(out->server->output_layout, out->output, &b);
    *box = b;
}

static void arrange_workspace(struct havel_wlr_server *server, uint32_t workspace_id) {
    if (!server) {
        return;
    }
    if (workspace_id >= HAVEL_WORKSPACE_COUNT) {
        return;
    }

    struct havel_output *out = primary_output(server);
    if (!out) {
        return;
    }

    struct wlr_box obox = {0};
    output_get_box(out, &obox);
    if (obox.width <= 0 || obox.height <= 0) {
        return;
    }

    int n = 0;
    struct havel_xdg_view *v = NULL;
    wl_list_for_each(v, &server->xdg_views_ws[workspace_id], ws_link) {
        if (v->mapped) {
            ++n;
        }
    }
    if (n <= 0) {
        return;
    }

    const int gap = 10;
    int x = obox.x + gap;
    int y = obox.y + gap;
    int w = obox.width - 2 * gap;
    int h = obox.height - 2 * gap;
    if (w < 1) {
        w = 1;
    }
    if (h < 1) {
        h = 1;
    }

    int master_w = w;
    int stack_w = 0;
    if (n > 1) {
        master_w = (w * 60) / 100;
        stack_w = w - master_w;
        if (master_w < 1) {
            master_w = 1;
        }
        if (stack_w < 1) {
            stack_w = 1;
        }
    }

    int i = 0;
    wl_list_for_each(v, &server->xdg_views_ws[workspace_id], ws_link) {
        if (!v->mapped || !v->xdg_surface || !v->xdg_surface->toplevel) {
            continue;
        }

        if (i == 0) {
            xdg_view_set_position(v, x, y);
            wlr_xdg_toplevel_set_size(v->xdg_surface->toplevel, master_w, h);
        } else {
            int stack_count = n - 1;
            int slot_h = (stack_count > 0) ? (h / stack_count) : h;
            if (slot_h < 1) {
                slot_h = 1;
            }
            int vy = y + (i - 1) * slot_h;
            int vh = (i == n - 1) ? (y + h - vy) : slot_h;
            if (vh < 1) {
                vh = 1;
            }
            xdg_view_set_position(v, x + master_w, vy);
            wlr_xdg_toplevel_set_size(v->xdg_surface->toplevel, stack_w, vh);
        }

        ++i;
    }
}

static void arrange_active_workspace(struct havel_wlr_server *server) {
    if (!server) {
        return;
    }

    arrange_workspace(server, server->active_workspace);
}

static void workspace_toggle_tiling(struct havel_wlr_server *server) {
    if (!server) {
        return;
    }

    uint32_t ws = server->active_workspace;
    if (ws >= HAVEL_WORKSPACE_COUNT) {
        return;
    }

    server->workspace_tiling_enabled[ws] = !server->workspace_tiling_enabled[ws];

    if (server->workspace_tiling_enabled[ws]) {
        arrange_workspace(server, ws);
        return;
    }

    struct havel_xdg_view *v = NULL;
    wl_list_for_each(v, &server->xdg_views_ws[ws], ws_link) {
        if (!v->mapped) {
            continue;
        }
        if (!v->have_float_geom) {
            continue;
        }
        if (!v->xdg_surface || !v->xdg_surface->toplevel) {
            continue;
        }
        xdg_view_set_position(v, v->float_x, v->float_y);
        if (v->float_w > 0 && v->float_h > 0) {
            wlr_xdg_toplevel_set_size(v->xdg_surface->toplevel, v->float_w, v->float_h);
        }
    }
}

static struct havel_xdg_view *workspace_first_view(struct havel_wlr_server *server, uint32_t workspace_id) {
    if (!server) {
        return NULL;
    }
    if (workspace_id >= HAVEL_WORKSPACE_COUNT) {
        return NULL;
    }
    if (wl_list_empty(&server->xdg_views_ws[workspace_id])) {
        return NULL;
    }
    struct havel_xdg_view *v = wl_container_of(server->xdg_views_ws[workspace_id].next, v, ws_link);
    if (!v->mapped) {
        return NULL;
    }
    return v;
}

static struct havel_xdg_view *workspace_next_view(struct havel_wlr_server *server, struct havel_xdg_view *cur, uint32_t workspace_id) {
    if (!server || !cur) {
        return NULL;
    }
    if (workspace_id >= HAVEL_WORKSPACE_COUNT) {
        return NULL;
    }

    struct wl_list *n = cur->ws_link.next;
    if (n == &server->xdg_views_ws[workspace_id]) {
        n = server->xdg_views_ws[workspace_id].next;
    }
    if (n == &server->xdg_views_ws[workspace_id]) {
        return NULL;
    }
    struct havel_xdg_view *v = wl_container_of(n, v, ws_link);
    if (!v->mapped) {
        return NULL;
    }
    return v;
}

static void workspace_swap_with_next(struct havel_wlr_server *server, bool backwards) {
    if (!server || !server->focused_xdg) {
        return;
    }

    struct havel_xdg_view *cur = server->focused_xdg;
    uint32_t ws = cur->workspace_id;
    if (ws >= HAVEL_WORKSPACE_COUNT) {
        return;
    }

    struct wl_list *other_link = backwards ? cur->ws_link.prev : cur->ws_link.next;
    if (other_link == &server->xdg_views_ws[ws]) {
        other_link = backwards ? server->xdg_views_ws[ws].prev : server->xdg_views_ws[ws].next;
    }
    if (other_link == &server->xdg_views_ws[ws]) {
        return;
    }

    wl_list_remove(&cur->ws_link);
    if (backwards) {
        wl_list_insert(other_link, &cur->ws_link);
    } else {
        wl_list_insert(other_link->prev, &cur->ws_link);
    }

    arrange_workspace(server, ws);
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

    if (server->grabbed_xdg && modifiers_have_alt_pressed(server)) {
        double dx = server->cursor->x - server->grab_cursor_x;
        double dy = server->cursor->y - server->grab_cursor_y;

        if (server->grab_button == BTN_LEFT) {
            xdg_view_set_position(server->grabbed_xdg,
                server->grab_view_x + (int)dx,
                server->grab_view_y + (int)dy);

            if (!server->workspace_tiling_enabled[server->grabbed_xdg->workspace_id]) {
                server->grabbed_xdg->float_x = server->grabbed_xdg->x;
                server->grabbed_xdg->float_y = server->grabbed_xdg->y;
                server->grabbed_xdg->float_w = server->grab_view_w;
                server->grabbed_xdg->float_h = server->grab_view_h;
                server->grabbed_xdg->have_float_geom = true;
            }
            return;
        }

        if (server->grab_button == BTN_RIGHT) {
            int w = server->grab_view_w + (int)dx;
            int h = server->grab_view_h + (int)dy;
            if (w < 1) {
                w = 1;
            }
            if (h < 1) {
                h = 1;
            }

            if (server->grabbed_xdg->xdg_surface && server->grabbed_xdg->xdg_surface->toplevel) {
                wlr_xdg_toplevel_set_size(server->grabbed_xdg->xdg_surface->toplevel, w, h);
            }

            if (!server->workspace_tiling_enabled[server->grabbed_xdg->workspace_id]) {
                server->grabbed_xdg->float_x = server->grabbed_xdg->x;
                server->grabbed_xdg->float_y = server->grabbed_xdg->y;
                server->grabbed_xdg->float_w = w;
                server->grabbed_xdg->float_h = h;
                server->grabbed_xdg->have_float_geom = true;
            }
            return;
        }
    }

    if (server->grabbed_xwayland && modifiers_have_meta_pressed(server)) {
        double dx = server->cursor->x - server->grab_cursor_x;
        double dy = server->cursor->y - server->grab_cursor_y;

        if (server->grab_button == BTN_LEFT) {
            int x = server->grab_view_x + (int)dx;
            int y = server->grab_view_y + (int)dy;
            wlr_xwayland_surface_configure(server->grabbed_xwayland->xsurface,
                x, y, server->grab_view_w, server->grab_view_h);
            xwayland_view_set_position(server->grabbed_xwayland, x, y);

            server->grabbed_xwayland->float_x = x;
            server->grabbed_xwayland->float_y = y;
            server->grabbed_xwayland->float_w = server->grab_view_w;
            server->grabbed_xwayland->float_h = server->grab_view_h;
            server->grabbed_xwayland->have_float_geom = true;
            return;
        }

        if (server->grab_button == BTN_RIGHT) {
            int w = server->grab_view_w + (int)dx;
            int h = server->grab_view_h + (int)dy;
            if (w < 1) {
                w = 1;
            }
            if (h < 1) {
                h = 1;
            }

            wlr_xwayland_surface_configure(server->grabbed_xwayland->xsurface,
                server->grab_view_x, server->grab_view_y, w, h);

            server->grabbed_xwayland->float_x = server->grab_view_x;
            server->grabbed_xwayland->float_y = server->grab_view_y;
            server->grabbed_xwayland->float_w = w;
            server->grabbed_xwayland->float_h = h;
            server->grabbed_xwayland->have_float_geom = true;
            return;
        }
    }

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

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED && server->grab_button == event->button) {
        server->grabbed_xdg = NULL;
        server->grabbed_xwayland = NULL;
        server->grab_button = 0;
        return;
    }

    if (event->state != WL_POINTER_BUTTON_STATE_PRESSED) {
        return;
    }

    double sx = 0.0, sy = 0.0;
    struct wlr_surface *surface = seat_surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
    if (surface) {
        struct havel_xdg_view *view = xdg_view_from_surface(surface);
        focus_xdg_view(server, view, surface);

        if (view && modifiers_have_alt_pressed(server) && (event->button == BTN_LEFT || event->button == BTN_RIGHT)) {
            server->grabbed_xdg = view;
            server->grabbed_xwayland = NULL;
            server->grab_button = event->button;
            server->grab_cursor_x = server->cursor->x;
            server->grab_cursor_y = server->cursor->y;
            server->grab_view_x = view->x;
            server->grab_view_y = view->y;
            server->grab_view_w = view->xdg_surface->current.geometry.width;
            server->grab_view_h = view->xdg_surface->current.geometry.height;

            if (!server->workspace_tiling_enabled[view->workspace_id]) {
                view->float_x = view->x;
                view->float_y = view->y;
                view->float_w = server->grab_view_w;
                view->float_h = server->grab_view_h;
                view->have_float_geom = true;
            }
            return;
        }

        if (server->xwayland) {
            struct wlr_xwayland_surface *xs = wlr_xwayland_surface_try_from_wlr_surface(surface);
            if (xs && xs->data && !xs->override_redirect) {
                struct havel_xwayland_view *xview = xs->data;
                wlr_scene_node_raise_to_top(&xview->scene_tree->node);
                wlr_xwayland_surface_activate(xs, true);

                if (modifiers_have_meta_pressed(server) && (event->button == BTN_LEFT || event->button == BTN_RIGHT)) {
                    server->grabbed_xdg = NULL;
                    server->grabbed_xwayland = xview;
                    server->grab_button = event->button;
                    server->grab_cursor_x = server->cursor->x;
                    server->grab_cursor_y = server->cursor->y;
                    server->grab_view_x = xview->x;
                    server->grab_view_y = xview->y;
                    server->grab_view_w = xs->width;
                    server->grab_view_h = xs->height;

                    xview->float_x = xview->x;
                    xview->float_y = xview->y;
                    xview->float_w = server->grab_view_w;
                    xview->float_h = server->grab_view_h;
                    xview->have_float_geom = true;
                    return;
                }
            }
        }
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

            if (meta && (syms[i] == XKB_KEY_y || syms[i] == XKB_KEY_Y)) {
                workspace_toggle_tiling(server);
                return;
            }

            if (!alt) {
                continue;
            }

            if (syms[i] == XKB_KEY_space) {
                if (server->focused_xdg && server->focused_xdg->workspace_id < HAVEL_WORKSPACE_COUNT) {
                    wl_list_remove(&server->focused_xdg->ws_link);
                    wl_list_insert(&server->xdg_views_ws[server->focused_xdg->workspace_id], &server->focused_xdg->ws_link);
                    arrange_workspace(server, server->focused_xdg->workspace_id);
                }
                return;
            }

            if (syms[i] == XKB_KEY_j) {
                if (shift) {
                    workspace_swap_with_next(server, false);
                    return;
                }
                if (server->focused_xdg) {
                    struct havel_xdg_view *next = workspace_next_view(server, server->focused_xdg, server->focused_xdg->workspace_id);
                    if (next && next->xdg_surface) {
                        focus_xdg_view(server, next, next->xdg_surface->surface);
                    }
                }
                return;
            }

            if (syms[i] == XKB_KEY_k) {
                if (shift) {
                    workspace_swap_with_next(server, true);
                    return;
                }
                if (server->focused_xdg) {
                    struct havel_xdg_view *prev = workspace_next_view(server, server->focused_xdg, server->focused_xdg->workspace_id);
                    if (prev && prev->xdg_surface) {
                        focus_xdg_view(server, prev, prev->xdg_surface->surface);
                    }
                }
                return;
            }

            if (syms[i] == XKB_KEY_h || syms[i] == XKB_KEY_l) {
                if (!server->focused_xdg) {
                    struct havel_xdg_view *first = workspace_first_view(server, server->active_workspace);
                    if (first && first->xdg_surface) {
                        focus_xdg_view(server, first, first->xdg_surface->surface);
                    }
                } else {
                    if (syms[i] == XKB_KEY_h) {
                        struct havel_xdg_view *first = workspace_first_view(server, server->focused_xdg->workspace_id);
                        if (first && first->xdg_surface) {
                            focus_xdg_view(server, first, first->xdg_surface->surface);
                        }
                    } else {
                        struct havel_xdg_view *first = workspace_first_view(server, server->focused_xdg->workspace_id);
                        if (first && first->xdg_surface) {
                            focus_xdg_view(server, first, first->xdg_surface->surface);
                        }
                    }
                }
                return;
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
    view->mapped = true;
    focus_xdg_view(view->server, view, view->xdg_surface->surface);
    if (view->server->workspace_tiling_enabled[view->workspace_id]) {
        arrange_workspace(view->server, view->workspace_id);
    } else {
        view_apply_default_floating_geom_xdg(view->server, view);
    }
}

static void xdg_view_handle_unmap(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, unmap);
    (void)data;
    view->mapped = false;

    if (view->parent) {
        wl_list_remove(&view->parent_link);
        wl_list_init(&view->parent_link);
        wl_list_remove(&view->parent_destroy.link);
        view->parent = NULL;
    }

    struct havel_xdg_view *child, *tmp;
    wl_list_for_each_safe(child, tmp, &view->children, parent_link) {
        if (child->xdg_surface && child->xdg_surface->toplevel) {
            wlr_xdg_toplevel_send_close(child->xdg_surface->toplevel);
        }
    }

    if (view->server->workspace_tiling_enabled[view->workspace_id]) {
        arrange_workspace(view->server, view->workspace_id);
    }
}

static void xdg_view_handle_destroy(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, destroy);
    struct havel_wlr_server *server = view->server;
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);

    if (view->parent) {
        wl_list_remove(&view->parent_link);
        wl_list_remove(&view->parent_destroy.link);
        view->parent = NULL;
    }

    struct havel_xdg_view *child, *tmp;
    wl_list_for_each_safe(child, tmp, &view->children, parent_link) {
        child->parent = NULL;
        wl_list_remove(&child->parent_link);
        wl_list_init(&child->parent_link);
        wl_list_remove(&child->parent_destroy.link);
    }

    if (server && server->focused_xdg == view) {
        server->focused_xdg = NULL;
    }

    if (!wl_list_empty(&view->mru_link)) {
        wl_list_remove(&view->mru_link);
        wl_list_init(&view->mru_link);
    }

    if (!wl_list_empty(&view->ws_link)) {
        wl_list_remove(&view->ws_link);
        wl_list_init(&view->ws_link);
    }
    free(view);
}

static void handle_parent_destroy(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, parent_destroy);
    wl_list_remove(&view->parent_link);
    wl_list_remove(&view->parent_destroy.link);
    view->parent = NULL;
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
    wl_list_init(&view->ws_link);
    wl_list_init(&view->children);
    wl_list_init(&view->parent_link);
    view->mapped = false;
    view->workspace_id = server->active_workspace;

    view->have_float_geom = false;
    view->float_x = 0;
    view->float_y = 0;
    view->float_w = 0;
    view->float_h = 0;

    if (xdg_surface->toplevel->parent) {
        struct havel_xdg_view *parent = xdg_surface->toplevel->parent->base->data;
        if (parent) {
            view->parent = parent;
            wl_list_insert(&parent->children, &view->parent_link);
            view->parent_destroy.notify = handle_parent_destroy;
            wl_signal_add(&parent->xdg_surface->events.destroy, &view->parent_destroy);
        }
    }

    if (view->workspace_id < HAVEL_WORKSPACE_COUNT) {
        wl_list_insert(&server->xdg_views_ws[view->workspace_id], &view->ws_link);
    }

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

    xdg_view_set_position(view, 0, 0);

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

    if (xsurface->override_redirect) {
        return;
    }

    struct havel_xwayland_view *view = calloc(1, sizeof(*view));
    view->xsurface = xsurface;
    xsurface->data = view;

    view->have_float_geom = false;
    view->float_x = 0;
    view->float_y = 0;
    view->float_w = 0;
    view->float_h = 0;

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
    wlr_scene_surface_create(view->scene_tree, xsurface->surface);

    bool has_position = (xsurface->x != 0 || xsurface->y != 0) &&
                        (xsurface->x != -1 && xsurface->y != -1);
    bool position_valid = false;
    if (has_position) {
        struct wlr_output *wlr_out = wlr_output_layout_output_at(server->output_layout,
                                                               xsurface->x, xsurface->y);
        position_valid = (wlr_out != NULL);
    }

    if (position_valid) {
        xwayland_view_set_position(view, xsurface->x, xsurface->y);
        view->float_x = xsurface->x;
        view->float_y = xsurface->y;
        view->float_w = xsurface->width;
        view->float_h = xsurface->height;
        view->have_float_geom = true;
    } else {
        xwayland_view_set_position(view, xsurface->x, xsurface->y);

        if (xsurface->width > 0 && xsurface->height > 0) {
            view->float_x = xsurface->x;
            view->float_y = xsurface->y;
            view->float_w = xsurface->width;
            view->float_h = xsurface->height;
            view->have_float_geom = true;
        }

        view_apply_default_floating_geom_xwayland(server, view);
    }

    view->destroy.notify = xwayland_view_handle_destroy;
    wl_signal_add(&xsurface->events.destroy, &view->destroy);
}

havel_wlr_server_t* havel_wlr_create(void) {
    wlr_log_init(WLR_DEBUG, NULL);

    struct havel_wlr_server *server = calloc(1, sizeof(*server));
    wl_list_init(&server->xdg_views_mru);
    wl_list_init(&server->outputs);
    for (uint32_t i = 0; i < HAVEL_WORKSPACE_COUNT; ++i) {
        wl_list_init(&server->xdg_views_ws[i]);
        server->workspace_tiling_enabled[i] = false;
    }
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
