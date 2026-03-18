// Scene Graph - High-Level Operations
// Layout, workspace management, output management

#include "SceneGraph.hpp"
#include <wm/bridge.h>
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>  // for offsetof

// Helper macros for safe casting
#define CONTAINER_FROM_NODE(node) ((SceneContainer*)((char*)(node) - offsetof(SceneContainer, base)))
#define VIEW_FROM_NODE(node) ((SceneView*)((char*)(node) - offsetof(SceneView, base)))
#define WORKSPACE_FROM_NODE(node) ((SceneWorkspace*)((char*)(node) - offsetof(SceneWorkspace, base)))

// ============================================================================
// Layout Engine (Tiling)
// ============================================================================

static void layout_container_recursive(SceneContainer* container, int x, int y, int w, int h) {
    if (!container) return;
    
    container->base.x = x;
    container->base.y = y;
    container->base.width = w;
    container->base.height = h;
    
    int total_children = container->child_container_count + container->child_view_count;
    if (total_children == 0) return;
    
    bool is_horizontal = (container->container_type == CONTAINER_SPLIT_H);
    int split_size = is_horizontal ? w : h;
    
    // Calculate child sizes
    int child_size = split_size / total_children;
    int remainder = split_size % total_children;
    
    int offset = 0;
    
    // Layout child containers
    SceneContainer* child_c = container->child_containers_head;
    while (child_c) {
        int extra = (remainder > 0) ? 1 : 0;
        int child_w = is_horizontal ? (child_size + extra) : w;
        int child_h = is_horizontal ? h : (child_size + extra);
        
        if (is_horizontal) {
            layout_container_recursive(child_c, x + offset, y, child_w, child_h);
            offset += child_w;
        } else {
            layout_container_recursive(child_c, x, y + offset, child_w, child_h);
            offset += child_h;
        }
        
        if (remainder > 0) remainder--;
        
        // Get next sibling
        child_c = child_c->base.next_sibling ? CONTAINER_FROM_NODE(child_c->base.next_sibling) : NULL;
    }
    
    // Layout child views
    SceneView* child_v = container->child_views_head;
    while (child_v) {
        int extra = (remainder > 0) ? 1 : 0;
        int child_w = is_horizontal ? (child_size + extra) : w;
        int child_h = is_horizontal ? h : (child_size + extra);
        
        if (is_horizontal) {
            child_v->base.x = x + offset;
            child_v->base.y = y;
            child_v->base.width = child_w;
            child_v->base.height = child_h;
            offset += child_w;
        } else {
            child_v->base.x = x;
            child_v->base.y = y + offset;
            child_v->base.width = child_w;
            child_v->base.height = child_h;
            offset += child_h;
        }
        
        if (remainder > 0) remainder--;
        
        // Get next sibling
        child_v = child_v->base.next_sibling ? VIEW_FROM_NODE(child_v->base.next_sibling) : NULL;
    }
}

void scene_layout_workspace(SceneWorkspace* workspace, int x, int y, int w, int h) {
    if (!workspace) return;
    
    // Layout floating views first
    SceneView* view = workspace->floating_views_head;
    while (view) {
        if (view->floating) {
            view->base.x = view->float_x;
            view->base.y = view->float_y;
            view->base.width = view->float_width;
            view->base.height = view->float_height;
        }
        view = view->base.next_sibling ? VIEW_FROM_NODE(view->base.next_sibling) : NULL;
    }
    
    // Layout tiled containers
    if (!workspace->containers_head) return;
    
    int container_count = 0;
    SceneContainer* c = workspace->containers_head;
    while (c) {
        container_count++;
        c = c->base.next_sibling ? CONTAINER_FROM_NODE(c->base.next_sibling) : NULL;
    }
    
    if (container_count == 0) return;
    
    int container_w = w / container_count;
    int remainder = w % container_count;
    int offset = 0;
    
    c = workspace->containers_head;
    while (c) {
        int cw = container_w + (remainder > 0 ? 1 : 0);
        layout_container_recursive(c, x + offset, y, cw, h);
        offset += cw;
        if (remainder > 0) remainder--;
        c = c->base.next_sibling ? CONTAINER_FROM_NODE(c->base.next_sibling) : NULL;
    }
}

// ============================================================================
// Workspace Operations
// ============================================================================

SceneWorkspace* scene_workspace_get(SceneOutput* output, uint32_t id) {
    if (!output || id >= SCENE_MAX_WORKSPACES) return NULL;
    return output->workspaces[id];
}

SceneWorkspace* scene_workspace_get_active(SceneOutput* output) {
    if (!output) return NULL;
    return scene_workspace_get(output, output->active_workspace_id);
}

bool scene_workspace_set_active(SceneOutput* output, uint32_t id, char* error_out, size_t error_size) {
    if (!output) {
        snprintf(error_out, error_size, "NULL output");
        return false;
    }
    
    if (id >= SCENE_MAX_WORKSPACES) {
        snprintf(error_out, error_size, "Invalid workspace ID %u", id);
        return false;
    }
    
    SceneWorkspace* ws = scene_workspace_get(output, id);
    if (!ws) {
        snprintf(error_out, error_size, "Workspace %u does not exist", id);
        return false;
    }
    
    output->active_workspace_id = id;
    ws->base.dirty_flags |= SCENE_DIRTY_LAYOUT;
    
    LOG_INFO("[Scene] Workspace %u activated on output", id);
    return true;
}

bool scene_workspace_add_view(SceneWorkspace* ws, SceneView* view, bool floating) {
    if (!ws || !view) return false;
    
    view->workspace = ws;
    view->floating = floating;
    
    if (floating) {
        // Add to floating list
        if (!ws->floating_views_head) {
            ws->floating_views_head = ws->floating_views_tail = view;
        } else {
            ws->floating_views_tail->base.next_sibling = &view->base;
            view->base.prev_sibling = &ws->floating_views_tail->base;
            ws->floating_views_tail = view;
        }
        ws->floating_count++;
    } else {
        // Add to first available container or create one
        if (!ws->containers_head) {
            LOG_ERROR("[Scene] No container for tiled view");
            return false;
        }
        
        // TODO: Add to container
        LOG_ERROR("[Scene] Container add not implemented");
        return false;
    }
    
    ws->base.dirty_flags |= SCENE_DIRTY_LAYOUT;
    return true;
}

bool scene_workspace_remove_view(SceneWorkspace* ws, SceneView* view) {
    if (!ws || !view) return false;
    
    if (view->floating) {
        // Remove from floating list
        if (view->base.prev_sibling) {
            view->base.prev_sibling->next_sibling = view->base.next_sibling;
        } else {
            ws->floating_views_head = view->base.next_sibling ? VIEW_FROM_NODE(view->base.next_sibling) : NULL;
        }
        
        if (view->base.next_sibling) {
            view->base.next_sibling->prev_sibling = view->base.prev_sibling;
        } else {
            ws->floating_views_tail = view->base.prev_sibling ? VIEW_FROM_NODE(view->base.prev_sibling) : NULL;
        }
        
        ws->floating_count--;
    } else {
        // Remove from container
        if (view->container) {
            // TODO: Remove from container
        }
    }
    
    view->workspace = NULL;
    view->container = NULL;
    ws->base.dirty_flags |= SCENE_DIRTY_LAYOUT;
    
    return true;
}

// ============================================================================
// Output Operations
// ============================================================================

SceneOutput* scene_output_get(Scene* scene, size_t index) {
    if (!scene || index >= scene->output_count) return NULL;
    return scene->outputs[index];
}

SceneOutput* scene_output_get_primary(Scene* scene) {
    return scene_output_get(scene, 0);
}

bool scene_output_configure(SceneOutput* output, int x, int y, float scale) {
    if (!output || !output->wlr_output) return false;
    
    output->base.x = x;
    output->base.y = y;
    output->zoom = scale;
    
    // Mark all workspaces dirty
    for (size_t i = 0; i < SCENE_MAX_WORKSPACES; i++) {
        if (output->workspaces[i]) {
            output->workspaces[i]->base.dirty_flags |= SCENE_DIRTY_LAYOUT;
        }
    }
    
    return true;
}

// ============================================================================
// Container Operations
// ============================================================================

SceneContainer* scene_container_create(SceneWorkspace* ws, ContainerType type) {
    if (!ws) return NULL;
    
    SceneContainer* container = (SceneContainer*)calloc(1, sizeof(SceneContainer));
    if (!container) return NULL;
    
    container->base.type = SCENE_NODE_CONTAINER;
    container->container_type = type;
    container->split_ratio = 0.5f;
    container->workspace = ws;
    
    // Add to workspace
    if (!ws->containers_head) {
        ws->containers_head = ws->containers_tail = container;
    } else {
        ws->containers_tail->base.next_sibling = &container->base;
        container->base.prev_sibling = &ws->containers_tail->base;
        ws->containers_tail = container;
    }
    ws->container_count++;
    
    ws->base.dirty_flags |= SCENE_DIRTY_CHILDREN;
    return container;
}

bool scene_container_destroy(SceneContainer* container) {
    if (!container || !container->workspace) return false;
    
    SceneWorkspace* ws = container->workspace;
    
    // Remove from workspace list
    if (container->base.prev_sibling) {
        container->base.prev_sibling->next_sibling = container->base.next_sibling;
    } else {
        ws->containers_head = container->base.next_sibling ? CONTAINER_FROM_NODE(container->base.next_sibling) : NULL;
    }
    
    if (container->base.next_sibling) {
        container->base.next_sibling->prev_sibling = container->base.prev_sibling;
    } else {
        ws->containers_tail = container->base.prev_sibling ? CONTAINER_FROM_NODE(container->base.prev_sibling) : NULL;
    }
    
    ws->container_count--;
    ws->base.dirty_flags |= SCENE_DIRTY_CHILDREN;
    
    // TODO: Move children to sibling container or parent
    
    free(container);
    return true;
}

bool scene_container_split(SceneContainer* container, ContainerType new_type) {
    if (!container) return false;
    
    // Create new parent container
    SceneContainer* parent = (SceneContainer*)calloc(1, sizeof(SceneContainer));
    if (!parent) return false;
    
    parent->base.type = SCENE_NODE_CONTAINER;
    parent->container_type = new_type;
    parent->split_ratio = 0.5f;
    parent->workspace = container->workspace;
    
    // Move existing container into new parent
    parent->child_containers_head = container;
    parent->child_containers_tail = container;
    parent->child_container_count = 1;
    
    container->base.parent = &parent->base;
    
    // TODO: Add parent to workspace
    
    return true;
}

// ============================================================================
// Scene Graph Integration
// ============================================================================

void scene_graph_update(Scene* scene) {
    if (!scene) return;

    // Update each output
    for (size_t i = 0; i < scene->output_count; i++) {
        SceneOutput* output = scene->outputs[i];

        // Update active workspace
        SceneWorkspace* ws = scene_workspace_get_active(output);
        if (ws && (ws->base.dirty_flags & SCENE_DIRTY_LAYOUT)) {
            scene_layout_workspace(ws, output->base.x, output->base.y,
                                   output->base.width, output->base.height);
            ws->base.dirty_flags &= ~SCENE_DIRTY_LAYOUT;
        }
    }
}

// ============================================================================
// Persistence - Save/Restore Layout
// ============================================================================

#include <stdio.h>
#include <string.h>

static void save_container_to_file(SceneContainer* container, FILE* f, int indent) {
    if (!container || !f) return;

    const char* type_str;
    switch (container->container_type) {
        case CONTAINER_SPLIT_H: type_str = "split_h"; break;
        case CONTAINER_SPLIT_V: type_str = "split_v"; break;
        case CONTAINER_TABBED: type_str = "tabbed"; break;
        case CONTAINER_STACKED: type_str = "stacked"; break;
        default: type_str = "unknown"; break;
    }

    // Write container opening
    fprintf(f, "%*s{\n", indent, "");
    fprintf(f, "%*s\"type\": \"container\",\n", indent + 2, "");
    fprintf(f, "%*s\"container_type\": \"%s\",\n", indent + 2, "", type_str);

    // Write children
    if (container->child_views_head || container->child_containers_head) {
        fprintf(f, "%*s\"children\": [\n", indent + 2, "");

        int first = 1;

        // Write view children
        SceneView* view = container->child_views_head;
        while (view) {
            if (!first) fprintf(f, ",\n");
            first = 0;
            fprintf(f, "%*s{\"type\": \"view\", \"app_id\": \"%s\", \"title\": \"%s\", \"floating\": %s}",
                    indent + 4, "",
                    view->app_id[0] ? view->app_id : "",
                    view->title[0] ? view->title : "",
                    view->floating ? "true" : "false");
            view = view->base.next_sibling ? (SceneView*)view->base.next_sibling : NULL;
        }

        // Write container children
        SceneContainer* child = container->child_containers_head;
        while (child) {
            if (!first) fprintf(f, ",\n");
            first = 0;
            save_container_to_file(child, f, indent + 4);
            child = child->base.next_sibling ? (SceneContainer*)child->base.next_sibling : NULL;
        }

        fprintf(f, "\n%*s]\n", indent + 2, "");
    }

    fprintf(f, "%*s}", indent, "");
}

bool scene_graph_save_layout(Scene* scene, const char* filename) {
    if (!scene || !filename) return false;

    FILE* f = fopen(filename, "w");
    if (!f) {
        LOG_ERROR("[Scene] Failed to open %s for writing", filename);
        return false;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": 1,\n");
    fprintf(f, "  \"outputs\": [\n");

    int output_first = 1;
    for (size_t i = 0; i < scene->output_count; i++) {
        SceneOutput* output = scene->outputs[i];
        if (!output) continue;

        if (!output_first) fprintf(f, ",\n");
        output_first = 0;

        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": \"output_%zu\",\n", i);
        fprintf(f, "      \"workspaces\": [\n");

        int ws_first = 1;
        for (uint32_t ws_id = 0; ws_id < SCENE_MAX_WORKSPACES; ws_id++) {
            SceneWorkspace* ws = scene_workspace_get(output, ws_id);
            if (!ws || (!ws->containers_head && !ws->floating_views_head)) continue;

            if (!ws_first) fprintf(f, ",\n");
            ws_first = 0;

            fprintf(f, "        {\n");
            fprintf(f, "          \"id\": %u,\n", ws_id);
            fprintf(f, "          \"active\": %s,\n", (ws_id == output->active_workspace_id) ? "true" : "false");

            // Save containers
            if (ws->containers_head) {
                fprintf(f, "          \"containers\": [\n");
                SceneContainer* container = ws->containers_head;
                int c_first = 1;
                while (container) {
                    if (!c_first) fprintf(f, ",\n");
                    c_first = 0;
                    save_container_to_file(container, f, 12);
                    container = container->base.next_sibling ? (SceneContainer*)container->base.next_sibling : NULL;
                }
                fprintf(f, "\n          ]\n");
            }

            // Save floating views
            if (ws->floating_views_head) {
                fprintf(f, "          \"floating_views\": [\n");
                SceneView* view = ws->floating_views_head;
                int v_first = 1;
                while (view) {
                    if (!v_first) fprintf(f, ",\n");
                    v_first = 0;
                    fprintf(f, "            {\"app_id\": \"%s\", \"title\": \"%s\", \"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d}",
                            view->app_id[0] ? view->app_id : "",
                            view->title[0] ? view->title : "",
                            view->float_x, view->float_y,
                            view->float_width, view->float_height);
                    view = view->base.next_sibling ? (SceneView*)view->base.next_sibling : NULL;
                }
                fprintf(f, "\n          ]\n");
            }

            fprintf(f, "        }");
        }

        fprintf(f, "\n      ]\n");
        fprintf(f, "    }");
    }

    fprintf(f, "\n  ]\n");
    fprintf(f, "}\n");

    fclose(f);
    LOG_INFO("[Scene] Layout saved to %s", filename);
    return true;
}

bool scene_graph_load_layout(Scene* scene, const char* filename) {
    // TODO: Implement JSON parsing for layout restore
    // For now, just log that the feature exists
    if (!scene || !filename) return false;

    FILE* f = fopen(filename, "r");
    if (!f) {
        LOG_INFO("[Scene] No saved layout found at %s (this is normal for first run)", filename);
        return false;
    }

    fclose(f);
    LOG_INFO("[Scene] Layout file exists at %s (parsing not yet implemented)", filename);

    // Full implementation would:
    // 1. Parse JSON
    // 2. For each output, match by name or create
    // 3. For each workspace, restore containers and views
    // 4. Match views by app_id/title to existing windows
    // 5. Restore container hierarchy and floating positions

    return true;  // File exists
}
