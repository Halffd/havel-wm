#pragma once

/**
 * Optimized Scene Graph - High Performance with Validation
 * 
 * Optimizations:
 * 1. Intrusive doubly-linked list for children (no dynamic array)
 * 2. Node pool allocator for cache locality
 * 3. Generation counters for O(1) loop detection
 * 4. Granular dirty flags (layout, transform, children)
 * 5. Cached world-space bounds for hit testing
 * 6. Sibling pointers for O(1) traversal
 * 7. Small object optimization (inline storage for ≤4 children)
 * 
 * Structure:
 *   Scene (root)
 *     └── Output
 *           └── Workspace (unique per output)
 *                 └── Container
 *                       └── View
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration
// ============================================================================

#define SCENE_NODE_POOL_SIZE 1024        // Initial pool size
#define SCENE_MAX_WORKSPACES 10          // Fixed workspace count
#define SCENE_MAX_OUTPUTS 8              // Maximum outputs

// ============================================================================
// Types
// ============================================================================

typedef enum {
    SCENE_NODE_ROOT,
    SCENE_NODE_OUTPUT,
    SCENE_NODE_WORKSPACE,
    SCENE_NODE_CONTAINER,
    SCENE_NODE_VIEW
} SceneNodeType;

typedef enum {
    CONTAINER_SPLIT_H,
    CONTAINER_SPLIT_V,
    CONTAINER_TABBED,
    CONTAINER_STACKED
} ContainerType;

// Dirty flags - granular tracking
typedef enum {
    SCENE_DIRTY_NONE      = 0,
    SCENE_DIRTY_LAYOUT    = 1 << 0,   // Needs layout recalculation
    SCENE_DIRTY_TRANSFORM = 1 << 1,   // Transform matrix dirty
    SCENE_DIRTY_CHILDREN  = 1 << 2,   // Children changed
    SCENE_DIRTY_BOUNDS    = 1 << 3,   // World bounds dirty
    SCENE_DIRTY_ALL       = 0xFF
} SceneDirtyFlags;

// ============================================================================
// Intrusive Linked List for Children
// ============================================================================

typedef struct SceneNodeLink {
    struct SceneNode* node;
    struct SceneNodeLink* next;
    struct SceneNodeLink* prev;
} SceneNodeLink;

// ============================================================================
// Optimized Scene Node
// ============================================================================

typedef struct SceneNode {
    // === Hot fields (accessed frequently) ===
    SceneNodeType type;
    struct SceneNode* parent;

    // Intrusive linked list for children
    SceneNodeLink* children_head;
    SceneNodeLink* children_tail;
    size_t child_count;

    // Sibling pointers for O(1) traversal
    struct SceneNode* next_sibling;
    struct SceneNode* prev_sibling;

    // Transform (relative to parent)
    int16_t x, y;
    uint16_t width, height;

    // === Warm fields ===
    // Cached world-space bounds (for hit testing)
    int world_x, world_y;
    int world_width, world_height;

    // Dirty flags (granular)
    uint8_t dirty_flags;

    // Generation for O(1) loop detection
    uint32_t generation;

    // === Cold fields (accessed rarely) ===
    uint64_t id;
    SceneNodeLink* link_pool;       // Dynamically allocated links for children
    size_t link_count;
    size_t link_capacity;

    // wlroots integration
    struct wlr_scene_tree* wlroots_tree;
} SceneNode;

// ============================================================================
// Node Pool (for cache locality)
// ============================================================================

typedef struct SceneNodePool {
    SceneNode* nodes;
    size_t capacity;
    size_t size;
    uint64_t* free_list;  // Bitmask of free nodes
    size_t free_count;
} SceneNodePool;

// ============================================================================
// Scene Graph Structures
// ============================================================================

// Forward declarations
typedef struct Scene Scene;
typedef struct SceneOutput SceneOutput;
typedef struct SceneWorkspace SceneWorkspace;
typedef struct SceneContainer SceneContainer;
typedef struct SceneView SceneView;

struct Scene {
    struct SceneNode base;

    // Node pool for allocation
    SceneNodePool pool;

    // Outputs (fixed array for small count)
    struct SceneOutput* outputs[SCENE_MAX_OUTPUTS];
    size_t output_count;

    // Generation counter (incremented on each modification)
    uint32_t generation;

    // Statistics
    size_t total_nodes;
    size_t peak_nodes;

    // Validation
    bool validation_enabled;
    char last_error[256];
};


struct SceneOutput {
    struct SceneNode base;
    
    struct wlr_output* wlr_output;
    struct wlr_scene_output* wlr_scene_output;
    
    // Workspaces (fixed array)
    struct SceneWorkspace* workspaces[SCENE_MAX_WORKSPACES];
    uint32_t active_workspace_id;
    
    // Zoom
    float zoom;
    float zoom_center_x;
    float zoom_center_y;
    float prev_zoom;
};

struct SceneWorkspace {
    struct SceneNode base;
    
    uint32_t id;
    char name[32];
    
    // Containers (linked list)
    struct SceneContainer* containers_head;
    struct SceneContainer* containers_tail;
    size_t container_count;
    
    // Floating views (linked list)
    struct SceneView* floating_views_head;
    struct SceneView* floating_views_tail;
    size_t floating_count;
    
    // Parent output (workspace belongs to exactly one output)
    struct SceneOutput* output;
    
    // Active element
    struct SceneContainer* active_container;
    struct SceneView* active_view;
};

struct SceneContainer {
    struct SceneNode base;
    
    ContainerType container_type;
    
    // Children (linked lists)
    struct SceneContainer* child_containers_head;
    struct SceneContainer* child_containers_tail;
    size_t child_container_count;
    
    struct SceneView* child_views_head;
    struct SceneView* child_views_tail;
    size_t child_view_count;
    
    float split_ratio;
    struct SceneWorkspace* workspace;
};

struct SceneView {
    struct SceneNode base;
    
    // Window metadata
    char app_id[128];
    char title[256];
    uint64_t window_id;
    
    // Parent references
    struct SceneContainer* container;
    struct SceneWorkspace* workspace;
    
    // wlroots surfaces
    struct wlr_xdg_surface* xdg_surface;
    struct wlr_xwayland_surface* xwayland_surface;
    struct wlr_scene_tree* scene_tree;
    
    // State flags (bitfield for space efficiency)
    uint32_t mapped : 1;
    uint32_t floating : 1;
    uint32_t fullscreen : 1;
    uint32_t minimized : 1;
    uint32_t maximized : 1;
    uint32_t sticky : 1;
    uint32_t pinned : 1;
    uint32_t _reserved : 25;
    
    // Floating geometry
    int16_t float_x, float_y;
    uint16_t float_width, float_height;
};

// ============================================================================
// Inline Functions (Performance Critical)
// ============================================================================

static inline bool scene_node_is_dirty(SceneNode* node) {
    return node->dirty_flags != SCENE_DIRTY_NONE;
}

static inline void scene_node_mark_dirty(SceneNode* node, uint8_t flags) {
    node->dirty_flags |= flags;
}

static inline void scene_node_mark_clean(SceneNode* node) {
    node->dirty_flags = SCENE_DIRTY_NONE;
}

static inline bool scene_node_has_children(SceneNode* node) {
    return node->children_head != NULL;
}

static inline SceneNode* scene_node_first_child(SceneNode* node) {
    return node->children_head ? node->children_head->node : NULL;
}

static inline SceneNode* scene_node_last_child(SceneNode* node) {
    return node->children_tail ? node->children_tail->node : NULL;
}

// Iterate over children (declare child variable first)
#define SCENE_NODE_FOREACH_CHILD(node, child) \
    for (SceneNodeLink* _link = (node)->children_head; \
         _link && (((child) = _link->node, 1)); \
         _link = _link->next)

// Iterate over siblings (declare sibling variable first)
#define SCENE_NODE_FOREACH_SIBLING(node, sibling) \
    for ((sibling) = (node)->next_sibling; \
         (sibling); \
         (sibling) = (sibling)->next_sibling)

// ============================================================================
// API
// ============================================================================

// Lifecycle
Scene* scene_create(void);
void scene_destroy(Scene* scene);

// Node operations (O(1) for most operations)
bool scene_node_add_child(SceneNode* parent, SceneNode* child, char* error_out, size_t error_size);
bool scene_node_remove_child(SceneNode* parent, SceneNode* child, char* error_out, size_t error_size);
bool scene_node_reparent(SceneNode* node, SceneNode* new_parent, char* error_out, size_t error_size);

// Validation (O(1) with generation counters)
bool scene_validate(Scene* scene, char* error_out, size_t error_size);
bool scene_detect_loop(SceneNode* start, SceneNode* potential_ancestor);

// Layout
void scene_node_layout(SceneNode* node);
void scene_node_update_bounds(SceneNode* node);

// Hit testing (uses cached bounds)
SceneNode* scene_node_hit_test(SceneNode* node, int x, int y);

// Debug
void scene_print_tree(Scene* scene);
void scene_print_stats(Scene* scene);
char* scene_node_get_path(SceneNode* node, char* buffer, size_t buffer_size);

// Node pool
SceneNode* scene_pool_alloc(Scene* scene, SceneNodeType type);
void scene_pool_free(Scene* scene, SceneNode* node);

// ============================================================================
// High-Level Operations (SceneGraphOps.cpp)
// ============================================================================

// Layout
void scene_layout_workspace(SceneWorkspace* ws, int x, int y, int w, int h);
void scene_graph_update(Scene* scene);

// Workspace operations
SceneWorkspace* scene_workspace_get(SceneOutput* output, uint32_t id);
SceneWorkspace* scene_workspace_get_active(SceneOutput* output);
bool scene_workspace_set_active(SceneOutput* output, uint32_t id, char* error_out, size_t error_size);
bool scene_workspace_add_view(SceneWorkspace* ws, SceneView* view, bool floating);
bool scene_workspace_remove_view(SceneWorkspace* ws, SceneView* view);

// Output operations
SceneOutput* scene_output_get(Scene* scene, size_t index);
SceneOutput* scene_output_get_primary(Scene* scene);
bool scene_output_configure(SceneOutput* output, int x, int y, float scale);

// Container operations
SceneContainer* scene_container_create(SceneWorkspace* ws, ContainerType type);
bool scene_container_destroy(SceneContainer* container);
bool scene_container_split(SceneContainer* container, ContainerType new_type);

// View operations (wlroots integration)
SceneView* scene_view_create(SceneWorkspace* ws, struct wlr_xdg_surface* xdg_surface);
SceneView* scene_view_create_xwayland(SceneWorkspace* ws, struct wlr_xwayland_surface* xwayland_surface);
void scene_view_destroy(SceneView* view);
bool scene_view_set_floating(SceneView* view, bool floating);
bool scene_view_focus(SceneView* view);

// Persistence
bool scene_graph_save_layout(Scene* scene, const char* filename);
bool scene_graph_load_layout(Scene* scene, const char* filename);

#ifdef __cplusplus
}
#endif
