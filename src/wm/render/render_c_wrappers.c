// Scene-graph drawing primitives — the C boundary between wlroots and
// the C++ OverlayRenderer. This file CAN include wlroots headers.

#include <wm/render_c.h>

#include <wlr/types/wlr_scene.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Minimal CPU-backed wlr_buffer: owns one ARGB8888 pixel array.
struct havel_cpu_buffer {
    struct wlr_buffer base;
    uint32_t* pixels;   // owned, width*height ARGB8888
    size_t stride;
    bool accessed;
};

static void havel_cpu_buffer_destroy(struct wlr_buffer* buf) {
    struct havel_cpu_buffer* b = (struct havel_cpu_buffer*)buf;
    free(b->pixels);
    free(b);
}

static bool havel_cpu_buffer_begin(struct wlr_buffer* buf, uint32_t flags,
                                   void** data, uint32_t* format, size_t* stride) {
    (void)flags;
    struct havel_cpu_buffer* b = (struct havel_cpu_buffer*)buf;
    if (b->accessed) return false;
    b->accessed = true;
    *data = b->pixels;
    *format = DRM_FORMAT_ARGB8888;
    *stride = b->stride;
    return true;
}

static void havel_cpu_buffer_end(struct wlr_buffer* buf) {
    struct havel_cpu_buffer* b = (struct havel_cpu_buffer*)buf;
    b->accessed = false;
}

static const struct wlr_buffer_impl havel_cpu_buffer_impl = {
    .destroy = havel_cpu_buffer_destroy,
    .get_dmabuf = NULL,
    .get_shm = NULL,
    .begin_data_ptr_access = havel_cpu_buffer_begin,
    .end_data_ptr_access = havel_cpu_buffer_end,
};

wlr_scene_tree_t* havel_scene_frame_tree_create(wlr_scene_tree_t* parent) {
    if (!parent) return NULL;
    struct wlr_scene_tree* tree = wlr_scene_tree_create((struct wlr_scene_tree*)parent);
    if (tree) {
        wlr_scene_node_raise_to_top(&tree->node);
    }
    return (wlr_scene_tree_t*)tree;
}

void havel_scene_frame_tree_destroy(wlr_scene_tree_t** tree) {
    if (!tree || !*tree) return;
    struct wlr_scene_tree* t = (struct wlr_scene_tree*)*tree;
    // Destroys the tree and all children (rects/buffers) with it.
    wlr_scene_node_destroy(&t->node);
    *tree = NULL;
}

wlr_scene_rect_t* havel_scene_rect(wlr_scene_tree_t* parent, int x, int y,
                                   int width, int height, const float color[4]) {
    if (!parent || !color || width < 0 || height < 0) return NULL;
    struct wlr_scene_rect* r = wlr_scene_rect_create(
        (struct wlr_scene_tree*)parent, width, height, color);
    if (r) {
        wlr_scene_node_set_position(&r->node, x, y);
    }
    return (wlr_scene_rect_t*)r;
}

wlr_scene_buffer_t* havel_scene_pixels(wlr_scene_tree_t* parent,
                                        const uint32_t* pixels, int pw, int ph,
                                        int x, int y, int width, int height) {
    if (!parent || !pixels || pw <= 0 || ph <= 0 || width <= 0 || height <= 0) {
        return NULL;
    }

    struct havel_cpu_buffer* b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->stride = (size_t)pw * sizeof(uint32_t);
    b->pixels = malloc((size_t)pw * ph * sizeof(uint32_t));
    if (!b->pixels) {
        free(b);
        return NULL;
    }
    memcpy(b->pixels, pixels, (size_t)pw * ph * sizeof(uint32_t));
    wlr_buffer_init(&b->base, &havel_cpu_buffer_impl, pw, ph);

    struct wlr_scene_buffer* sb = wlr_scene_buffer_create(
        (struct wlr_scene_tree*)parent, &b->base);
    if (!sb) {
        wlr_buffer_finish(&b->base);
        free(b->pixels);
        free(b);
        return NULL;
    }
    // Scene buffer holds a reference; drop ours.
    wlr_buffer_drop(&b->base);
    wlr_scene_buffer_set_dest_size(sb, width, height);
    wlr_scene_node_set_position(&sb->node, x, y);
    return (wlr_scene_buffer_t*)sb;
}
