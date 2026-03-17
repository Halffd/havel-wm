// Scene Graph - Container and View Operations
// Note: View creation with wlroots surfaces is in wlr_bridge.c

#include "SceneGraph.hpp"
#include <wm/bridge.h>
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// View creation is implemented in wlr_bridge.c:
// SceneView* scene_view_create(SceneWorkspace* ws, struct wlr_xdg_surface* xdg_surface);
// SceneView* scene_view_create_xwayland(SceneWorkspace* ws, struct wlr_xwayland_surface* xwayland_surface);

// ============================================================================
// Container Operations
// ============================================================================

SceneContainer* scene_container_create(SceneWorkspace* ws, ContainerType type) {
    if (!ws) {
        LOG_ERROR("[Scene] NULL workspace for container creation");
        return NULL;
    }
    
    SceneContainer* container = (SceneContainer*)calloc(1, sizeof(SceneContainer));
    if (!container) {
        LOG_ERROR("[Scene] Failed to allocate container");
        return NULL;
    }
    
    Scene* scene = (Scene*)ws->base.parent;
    init_scene_node(&container->base, SCENE_NODE_CONTAINER, scene);
    
    container->container_type = type;
    container->child_containers = NULL;
    container->child_views = NULL;
    container->child_container_count = 0;
    container->child_view_count = 0;
    container->split_ratio = 0.5f;
    container->workspace = ws;
    
    // Add to workspace
    char error[256];
    if (!scene_workspace_add_container(ws, container, error, sizeof(error))) {
        LOG_ERROR("[Scene] Failed to add container to workspace: %s", error);
        free(container);
        return NULL;
    }
    
    LOG_INFO("[Scene] Created container type=%d in workspace %u (id=%lu)", 
             type, ws->id, (unsigned long)container->base.id);
    return container;
}

void scene_container_destroy(SceneContainer* container) {
    if (!container) return;
    
    LOG_INFO("[Scene] Destroying container id=%lu", (unsigned long)container->base.id);
    
    // Destroy child containers recursively
    for (size_t i = 0; i < container->child_container_count; i++) {
        scene_container_destroy(container->child_containers[i]);
    }
    
    // Destroy child views
    for (size_t i = 0; i < container->child_view_count; i++) {
        scene_view_destroy(container->child_views[i]);
    }
    
    free(container->child_containers);
    free(container->child_views);
    free(container->base.children);
    
    // Remove from workspace
    SceneWorkspace* ws = container->workspace;
    if (ws) {
        for (size_t i = 0; i < ws->container_count; i++) {
            if (ws->containers[i] == container) {
                for (size_t j = i + 1; j < ws->container_count; j++) {
                    ws->containers[j - 1] = ws->containers[j];
                }
                ws->container_count--;
                break;
            }
        }
    }
    
    free(container);
}

bool scene_container_add_view(SceneContainer* container, SceneView* view, 
                               char* error_out, size_t error_size) {
    if (!container || !view) {
        snprintf(error_out, error_size, "NULL container or view");
        return false;
    }
    
    // Check if view already has a container
    if (view->container != NULL) {
        snprintf(error_out, error_size, "View already belongs to a container");
        return false;
    }
    
    // Grow child_views array
    if (container->child_view_count >= container->child_view_capacity) {
        size_t new_capacity = container->child_view_capacity == 0 ? 4 : container->child_view_capacity * 2;
        SceneView** new_views = (SceneView**)realloc(container->child_views, new_capacity * sizeof(SceneView*));
        if (!new_views) {
            snprintf(error_out, error_size, "Out of memory");
            return false;
        }
        container->child_views = new_views;
        container->child_view_capacity = new_capacity;
    }
    
    // Add view to container
    container->child_views[container->child_view_count++] = view;
    view->container = container;
    view->workspace = NULL;  // View is now in container, not directly in workspace
    
    // Add to container's scene node children
    add_child_to_node(&container->base, &view->base, error_out, error_size);
    
    container->base.dirty = true;
    LOG_DEBUG("[Scene] Added view to container (container=%lu, view=%lu)",
              (unsigned long)container->base.id, (unsigned long)view->base.id);
    return true;
}

bool scene_container_add_container(SceneContainer* parent, SceneContainer* child,
                                    char* error_out, size_t error_size) {
    if (!parent || !child) {
        snprintf(error_out, error_size, "NULL parent or child container");
        return false;
    }
    
    // Check for loops
    if (scene_detect_loop(&parent->base, &child->base)) {
        snprintf(error_out, error_size, "Loop detected");
        return false;
    }
    
    // Grow child_containers array
    if (parent->child_container_count >= parent->child_container_capacity) {
        size_t new_capacity = parent->child_container_capacity == 0 ? 4 : parent->child_container_capacity * 2;
        SceneContainer** new_containers = (SceneContainer**)realloc(parent->child_containers, new_capacity * sizeof(SceneContainer*));
        if (!new_containers) {
            snprintf(error_out, error_size, "Out of memory");
            return false;
        }
        parent->child_containers = new_containers;
        parent->child_container_capacity = new_capacity;
    }
    
    // Add child to parent
    parent->child_containers[parent->child_container_count++] = child;
    
    // Add to scene node children
    add_child_to_node(&parent->base, &child->base, error_out, error_size);
    
    parent->base.dirty = true;
    return true;
}

bool scene_container_remove_view(SceneContainer* container, SceneView* view,
                                  char* error_out, size_t error_size) {
    if (!container || !view) {
        snprintf(error_out, error_size, "NULL container or view");
        return false;
    }
    
    // Find and remove view
    size_t found_idx = SIZE_MAX;
    for (size_t i = 0; i < container->child_view_count; i++) {
        if (container->child_views[i] == view) {
            found_idx = i;
            break;
        }
    }
    
    if (found_idx == SIZE_MAX) {
        snprintf(error_out, error_size, "View not in container");
        return false;
    }
    
    // Remove from array
    for (size_t i = found_idx; i < container->child_view_count - 1; i++) {
        container->child_views[i] = container->child_views[i + 1];
    }
    container->child_view_count--;
    
    // Clear view's container pointer
    view->container = NULL;
    
    // Remove from scene node children
    scene_node_remove_child(&container->base, &view->base, error_out, error_size);
    
    container->base.dirty = true;
    return true;
}

SceneContainer* scene_container_split(SceneContainer* container, ContainerType new_type) {
    if (!container) return NULL;
    
    SceneWorkspace* ws = container->workspace;
    if (!ws) {
        LOG_ERROR("[Scene] Container has no workspace");
        return NULL;
    }
    
    // Create new parent container with the new split type
    SceneContainer* parent = scene_container_create(ws, new_type);
    if (!parent) return NULL;
    
    LOG_INFO("[Scene] Split container into type=%d", new_type);
    return parent;
}

// ============================================================================
// View Operations
// ============================================================================

void scene_view_destroy(SceneView* view) {
    if (!view) return;
    
    LOG_INFO("[Scene] Destroying view '%s' id=%lu", view->app_id, (unsigned long)view->base.id);
    
    // Remove from container if in one
    if (view->container) {
        char error[256];
        scene_container_remove_view(view->container, view, error, sizeof(error));
    }
    
    // Remove from workspace floating list
    if (view->workspace) {
        SceneWorkspace* ws = view->workspace;
        for (size_t i = 0; i < ws->floating_count; i++) {
            if (ws->floating_views[i] == view) {
                for (size_t j = i + 1; j < ws->floating_count; j++) {
                    ws->floating_views[j - 1] = ws->floating_views[j];
                }
                ws->floating_count--;
                break;
            }
        }
    }
    
    free(view->base.children);
    free(view);
}

bool scene_view_set_floating(SceneView* view, bool floating, char* error_out, size_t error_size) {
    if (!view) {
        snprintf(error_out, error_size, "NULL view");
        return false;
    }
    
    if (floating && !view->floating) {
        // Save current geometry for restore
        view->float_x = view->base.x;
        view->float_y = view->base.y;
        view->float_width = view->base.width;
        view->float_height = view->base.height;
    }
    
    view->floating = floating;
    view->base.dirty = true;
    
    LOG_DEBUG("[Scene] View %lu floating=%s", (unsigned long)view->base.id, floating ? "yes" : "no");
    return true;
}

bool scene_view_focus(SceneView* view, char* error_out, size_t error_size) {
    (void)error_out; (void)error_size;
    
    if (!view) return false;
    
    if (view->workspace) {
        view->workspace->active_view = view;
        
        if (view->container) {
            view->workspace->active_container = view->container;
        }
    }
    
    LOG_DEBUG("[Scene] Focused view %lu", (unsigned long)view->base.id);
    return true;
}

// ============================================================================
// Node Add/Remove
// ============================================================================

bool scene_node_remove_child(SceneNode* parent, SceneNode* child, char* error_out, size_t error_size) {
    if (!parent || !child) {
        snprintf(error_out, error_size, "NULL parent or child");
        return false;
    }
    
    if (child->parent != parent) {
        snprintf(error_out, error_size, "Child's parent doesn't match");
        return false;
    }
    
    // Find and remove from parent's children array
    size_t found_idx = SIZE_MAX;
    for (size_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            found_idx = i;
            break;
        }
    }
    
    if (found_idx == SIZE_MAX) {
        snprintf(error_out, error_size, "Child not found in parent");
        return false;
    }
    
    // Remove from array
    for (size_t i = found_idx; i < parent->child_count - 1; i++) {
        parent->children[i] = parent->children[i + 1];
    }
    parent->child_count--;
    child->parent = NULL;
    
    parent->dirty = true;
    return true;
}

bool scene_node_reparent(SceneNode* node, SceneNode* new_parent, char* error_out, size_t error_size) {
    if (!node || !new_parent) {
        snprintf(error_out, error_size, "NULL node or new_parent");
        return false;
    }
    
    // Check for loops
    if (scene_detect_loop(new_parent, node)) {
        snprintf(error_out, error_size, "Loop detected");
        return false;
    }
    
    // Remove from old parent
    if (node->parent) {
        if (!scene_node_remove_child(node->parent, node, error_out, error_size)) {
            return false;
        }
    }
    
    // Add to new parent
    return add_child_to_node(new_parent, node, error_out, error_size);
}

// ============================================================================
// Workspace Add View/Container
// ============================================================================

bool scene_workspace_add_container(SceneWorkspace* ws, SceneContainer* container, 
                                    char* error_out, size_t error_size) {
    if (!ws || !container) {
        snprintf(error_out, error_size, "NULL workspace or container");
        return false;
    }
    
    if (container->workspace != NULL && container->workspace != ws) {
        snprintf(error_out, error_size, "Container belongs to different workspace");
        return false;
    }
    
    // Grow containers array
    if (ws->container_count >= ws->container_capacity) {
        size_t new_capacity = ws->container_capacity == 0 ? 4 : ws->container_capacity * 2;
        SceneContainer** new_containers = (SceneContainer**)realloc(ws->containers, new_capacity * sizeof(SceneContainer*));
        if (!new_containers) {
            snprintf(error_out, error_size, "Out of memory");
            return false;
        }
        ws->containers = new_containers;
        ws->container_capacity = new_capacity;
    }
    
    ws->containers[ws->container_count++] = container;
    container->workspace = ws;
    
    // Add to scene node children
    add_child_to_node(&ws->base, &container->base, error_out, error_size);
    
    ws->base.dirty = true;
    return true;
}

bool scene_workspace_add_view(SceneWorkspace* ws, SceneView* view, SceneContainer* container,
                               char* error_out, size_t error_size) {
    (void)container;  // For now, add as floating
    
    if (!ws || !view) {
        snprintf(error_out, error_size, "NULL workspace or view");
        return false;
    }
    
    if (view->workspace != NULL && view->workspace != ws) {
        snprintf(error_out, error_size, "View belongs to different workspace");
        return false;
    }
    
    // Grow floating_views array
    if (ws->floating_count >= ws->floating_capacity) {
        size_t new_capacity = ws->floating_capacity == 0 ? 4 : ws->floating_capacity * 2;
        SceneView** new_views = (SceneView**)realloc(ws->floating_views, new_capacity * sizeof(SceneView*));
        if (!new_views) {
            snprintf(error_out, error_size, "Out of memory");
            return false;
        }
        ws->floating_views = new_views;
        ws->floating_capacity = new_capacity;
    }
    
    ws->floating_views[ws->floating_count++] = view;
    view->workspace = ws;
    
    // Add to scene node children
    add_child_to_node(&ws->base, &view->base, error_out, error_size);
    
    ws->base.dirty = true;
    return true;
}

bool scene_workspace_move_view(SceneView* view, SceneWorkspace* new_ws, 
                                char* error_out, size_t error_size) {
    if (!view || !new_ws) {
        snprintf(error_out, error_size, "NULL view or new workspace");
        return false;
    }
    
    SceneWorkspace* old_ws = view->workspace;
    
    // Remove from old workspace
    if (old_ws && old_ws != new_ws) {
        for (size_t i = 0; i < old_ws->floating_count; i++) {
            if (old_ws->floating_views[i] == view) {
                for (size_t j = i + 1; j < old_ws->floating_count; j++) {
                    old_ws->floating_views[j - 1] = old_ws->floating_views[j];
                }
                old_ws->floating_count--;
                break;
            }
        }
        
        // Remove from scene node children
        scene_node_remove_child(&old_ws->base, &view->base, error_out, error_size);
    }
    
    // Add to new workspace
    return scene_workspace_add_view(new_ws, view, NULL, error_out, error_size);
}
