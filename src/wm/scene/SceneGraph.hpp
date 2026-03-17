#pragma once

/**
 * True Scene Graph - Proper Hierarchy with Validation
 * 
 * Structure:
 *   Scene (root, 1)
 *     └── Output (N)
 *           └── Workspace (unique across all outputs)
 *                 └── Container (tiling splits, stacks, tabs)
 *                       └── View (window surface)
 * 
 * Validation Rules:
 *   1. No loops (child cannot be ancestor of parent)
 *   2. Parent pointer matches child's actual parent
 *   3. Nothing points TO root (root only points TO outputs)
 *   4. Workspaces are unique (each workspace belongs to exactly one output)
 *   5. View has exactly one parent (container or workspace)
 *   6. Container has exactly one parent (workspace or container)
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct Scene Scene;
typedef struct SceneOutput SceneOutput;
typedef struct SceneWorkspace SceneWorkspace;
typedef struct SceneContainer SceneContainer;
typedef struct SceneView SceneView;
typedef struct SceneNode SceneNode;

// Node types
typedef enum {
    SCENE_NODE_ROOT,
    SCENE_NODE_OUTPUT,
    SCENE_NODE_WORKSPACE,
    SCENE_NODE_CONTAINER,
    SCENE_NODE_VIEW
} SceneNodeType;

// Container types (for tiling)
typedef enum {
    CONTAINER_SPLIT_H,      // Horizontal split (side by side)
    CONTAINER_SPLIT_V,      // Vertical split (stacked)
    CONTAINER_TABBED,       // Tabbed container
    CONTAINER_STACKED       // Stacked container
} ContainerType;

// Base node - all scene nodes inherit from this
struct SceneNode {
    SceneNodeType type;
    SceneNode* parent;          // NULL for root
    SceneNode** children;       // Dynamic array
    size_t child_count;
    size_t child_capacity;
    
    // Position/size (relative to parent)
    int x, y, width, height;
    
    // Unique ID for debugging/validation
    uint64_t id;
    
    // Validation flags
    bool validated;
    bool dirty;  // Needs layout recalculation
    
    // wlroots integration
    struct wlr_scene_tree* wlroots_tree;
};

// Scene (root)
struct Scene {
    SceneNode base;
    
    // Outputs attached to this scene
    SceneOutput** outputs;
    size_t output_count;
    size_t output_capacity;
    
    // Global ID counter
    uint64_t next_id;
    
    // Validation state
    bool validation_enabled;
    char last_validation_error[256];
};

// Output (monitor)
struct SceneOutput {
    SceneNode base;
    
    struct wlr_output* wlr_output;
    struct wlr_scene_output* wlr_scene_output;
    
    // Workspaces for this output (unique across all outputs)
    SceneWorkspace** workspaces;
    size_t workspace_count;
    size_t workspace_capacity;
    
    uint32_t active_workspace_id;
    
    // Output-local zoom
    float zoom;
    float zoom_center_x;
    float zoom_center_y;
};

// Workspace (collection of containers/views)
struct SceneWorkspace {
    SceneNode base;
    
    uint32_t id;  // Workspace number (0-9)
    char name[32];
    
    // Containers in this workspace
    SceneContainer** containers;
    size_t container_count;
    size_t container_capacity;
    
    // Floating views (not in containers)
    SceneView** floating_views;
    size_t floating_count;
    size_t floating_capacity;
    
    // Which output this workspace belongs to (unique!)
    SceneOutput* output;
    
    // Active container/view
    SceneContainer* active_container;
    SceneView* active_view;
};

// Container (tiling group)
struct SceneContainer {
    SceneNode base;
    
    ContainerType container_type;
    
    // Child containers or views
    SceneContainer** child_containers;
    SceneView** child_views;
    size_t child_container_count;
    size_t child_container_capacity;
    size_t child_view_count;
    size_t child_view_capacity;
    
    // Split ratio (for split containers)
    float split_ratio;  // 0.0 - 1.0
    
    // Parent workspace
    SceneWorkspace* workspace;
};

// View (window surface)
struct SceneView {
    SceneNode base;
    
    // Window metadata
    char app_id[128];
    char title[256];
    uint64_t window_id;
    
    // Parent container (NULL if floating)
    SceneContainer* container;
    
    // Parent workspace (for floating views)
    SceneWorkspace* workspace;
    
    // wlroots surface
    struct wlr_xdg_surface* xdg_surface;
    struct wlr_xwayland_surface* xwayland_surface;
    struct wlr_scene_tree* scene_tree;
    
    // View state
    bool mapped;
    bool floating;
    bool fullscreen;
    bool minimized;
    bool maximized;
    
    // Floating geometry (saved when tiled)
    int float_x, float_y, float_width, float_height;
};

// ============================================================================
// Internal Helpers (exposed for SceneGraphOps.cpp)
// ============================================================================

bool add_child_to_node(SceneNode* parent, SceneNode* child, char* error_out, size_t error_size);
void init_scene_node(SceneNode* node, SceneNodeType type, Scene* scene);

// ============================================================================
// Scene Graph Lifecycle
// ============================================================================

Scene* scene_create(void);
void scene_destroy(Scene* scene);

// ============================================================================
// Node Operations (with validation)
// ============================================================================

// Add child to parent (validates: no loops, parent not null)
bool scene_node_add_child(SceneNode* parent, SceneNode* child, char* error_out, size_t error_size);

// Remove child from parent
bool scene_node_remove_child(SceneNode* parent, SceneNode* child, char* error_out, size_t error_size);

// Reparent node (move to new parent)
bool scene_node_reparent(SceneNode* node, SceneNode* new_parent, char* error_out, size_t error_size);

// ============================================================================
// Output Operations
// ============================================================================

SceneOutput* scene_output_create(Scene* scene, struct wlr_output* wlr_output);
void scene_output_destroy(SceneOutput* output);

// Get workspace on this output (creates if doesn't exist)
SceneWorkspace* scene_output_get_workspace(SceneOutput* output, uint32_t workspace_id);

// Set active workspace for output
bool scene_output_set_active_workspace(SceneOutput* output, uint32_t workspace_id, char* error_out, size_t error_size);

// ============================================================================
// Workspace Operations
// ============================================================================

SceneWorkspace* scene_workspace_create(SceneOutput* output, uint32_t id);
void scene_workspace_destroy(SceneWorkspace* workspace);

// Add container to workspace
bool scene_workspace_add_container(SceneWorkspace* ws, SceneContainer* container, char* error_out, size_t error_size);

// Add view to workspace (as floating or in container)
bool scene_workspace_add_view(SceneWorkspace* ws, SceneView* view, SceneContainer* container, char* error_out, size_t error_size);

// Move view to different workspace
bool scene_workspace_move_view(SceneView* view, SceneWorkspace* new_ws, char* error_out, size_t error_size);

// ============================================================================
// Container Operations
// ============================================================================

SceneContainer* scene_container_create(SceneWorkspace* ws, ContainerType type);
void scene_container_destroy(SceneContainer* container);

// Add view to container
bool scene_container_add_view(SceneContainer* container, SceneView* view, char* error_out, size_t error_size);

// Add container to container (nesting)
bool scene_container_add_container(SceneContainer* parent, SceneContainer* child, char* error_out, size_t error_size);

// Remove view from container
bool scene_container_remove_view(SceneContainer* container, SceneView* view, char* error_out, size_t error_size);

// Split container (create new split with existing content)
SceneContainer* scene_container_split(SceneContainer* container, ContainerType new_type);

// ============================================================================
// View Operations
// ============================================================================

SceneView* scene_view_create(SceneWorkspace* ws, struct wlr_xdg_surface* xdg_surface);
SceneView* scene_view_create_xwayland(SceneWorkspace* ws, struct wlr_xwayland_surface* xwayland_surface);
void scene_view_destroy(SceneView* view);

// Set view as floating/tiled
bool scene_view_set_floating(SceneView* view, bool floating, char* error_out, size_t error_size);

// Focus view
bool scene_view_focus(SceneView* view, char* error_out, size_t error_size);

// ============================================================================
// Validation
// ============================================================================

// Validate entire scene graph
bool scene_validate(Scene* scene, char* error_out, size_t error_size);

// Validate single node and its relationships
bool scene_node_validate(SceneNode* node, char* error_out, size_t error_size);

// Check for loops (returns true if loop detected)
bool scene_detect_loop(SceneNode* start, SceneNode* potential_ancestor);

// Get node path (for debugging)
char* scene_node_get_path(SceneNode* node, char* buffer, size_t buffer_size);

// ============================================================================
// Layout
// ============================================================================

// Recalculate layout for node and children
void scene_node_layout(SceneNode* node);

// Mark node as needing layout
void scene_node_mark_dirty(SceneNode* node);

// ============================================================================
// Debug/Introspection
// ============================================================================

// Print scene graph tree
void scene_print_tree(Scene* scene);
void scene_node_print_tree(SceneNode* node, int depth);

// Get node count by type
size_t scene_count_nodes(Scene* scene, SceneNodeType type);

// Find node by ID
SceneNode* scene_find_node_by_id(Scene* scene, uint64_t id);

#ifdef __cplusplus
}
#endif
