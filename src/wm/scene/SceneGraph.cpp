// True Scene Graph Implementation with Validation

#include "SceneGraph.hpp"
#include <wm/bridge.h>
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// wlroots types are opaque pointers - we don't need to include headers here
// The actual wlr_output usage is in wlr_bridge.c which includes this

// ============================================================================
// Internal Helpers
// ============================================================================

static uint64_t generate_node_id(Scene* scene) {
    return ++scene->next_id;
}

void init_scene_node(SceneNode* node, SceneNodeType type, Scene* scene) {
    node->type = type;
    node->parent = NULL;
    node->children = NULL;
    node->child_count = 0;
    node->child_capacity = 0;
    node->x = node->y = 0;
    node->width = node->height = 0;
    node->id = generate_node_id(scene);
    node->validated = false;
    node->dirty = true;
    node->wlroots_tree = NULL;
}

bool add_child_to_node(SceneNode* parent, SceneNode* child, char* error_out, size_t error_size) {
    if (!parent || !child) {
        snprintf(error_out, error_size, "NULL parent or child");
        return false;
    }
    
    // Check for loops
    if (scene_detect_loop(parent, child)) {
        snprintf(error_out, error_size, "Loop detected: cannot add node as child of its descendant");
        return false;
    }
    
    // Check if child already has a parent
    if (child->parent != NULL) {
        snprintf(error_out, error_size, "Child already has a parent (type=%d)", child->parent->type);
        return false;
    }
    
    // Grow children array if needed
    if (parent->child_count >= parent->child_capacity) {
        size_t new_capacity = parent->child_capacity == 0 ? 4 : parent->child_capacity * 2;
        SceneNode** new_children = (SceneNode**)realloc(parent->children, new_capacity * sizeof(SceneNode*));
        if (!new_children) {
            snprintf(error_out, error_size, "Out of memory");
            return false;
        }
        parent->children = new_children;
        parent->child_capacity = new_capacity;
    }
    
    // Add child
    parent->children[parent->child_count++] = child;
    child->parent = parent;
    child->dirty = true;
    parent->dirty = true;
    
    return true;
}

// ============================================================================
// Scene Lifecycle
// ============================================================================

Scene* scene_create(void) {
    Scene* scene = (Scene*)calloc(1, sizeof(Scene));
    if (!scene) {
        LOG_ERROR("[Scene] Failed to allocate scene");
        return NULL;
    }
    
    init_scene_node(&scene->base, SCENE_NODE_ROOT, scene);
    scene->outputs = NULL;
    scene->output_count = 0;
    scene->output_capacity = 0;
    scene->next_id = 0;
    scene->validation_enabled = true;
    scene->last_validation_error[0] = '\0';
    
    LOG_INFO("[Scene] Created scene graph (root id=%lu)", (unsigned long)scene->base.id);
    return scene;
}

void scene_destroy(Scene* scene) {
    if (!scene) return;
    
    LOG_INFO("[Scene] Destroying scene graph");
    
    // Destroy all outputs (which destroys their workspaces, containers, views)
    for (size_t i = 0; i < scene->output_count; i++) {
        scene_output_destroy(scene->outputs[i]);
    }
    
    free(scene->outputs);
    free(scene->base.children);
    free(scene);
}

// ============================================================================
// Validation
// ============================================================================

bool scene_detect_loop(SceneNode* start, SceneNode* potential_ancestor) {
    if (!start || !potential_ancestor) return false;
    
    // Walk up from start to see if we hit potential_ancestor
    SceneNode* current = start;
    while (current != NULL) {
        if (current == potential_ancestor) {
            return true;  // Loop detected
        }
        current = current->parent;
    }
    
    return false;
}

bool scene_node_validate(SceneNode* node, char* error_out, size_t error_size) {
    if (!node) {
        snprintf(error_out, error_size, "NULL node");
        return false;
    }
    
    // Root cannot have a parent
    if (node->type == SCENE_NODE_ROOT && node->parent != NULL) {
        snprintf(error_out, error_size, "Root node has a parent (type=%d)", node->parent->type);
        return false;
    }
    
    // Non-root must have a parent
    if (node->type != SCENE_NODE_ROOT && node->parent == NULL) {
        snprintf(error_out, error_size, "Non-root node (type=%d) has NULL parent", node->type);
        return false;
    }
    
    // Verify parent's children array contains this node
    if (node->parent) {
        bool found = false;
        for (size_t i = 0; i < node->parent->child_count; i++) {
            if (node->parent->children[i] == node) {
                found = true;
                break;
            }
        }
        if (!found) {
            snprintf(error_out, error_size, "Parent's children array doesn't contain this node");
            return false;
        }
    }
    
    // Check for loops
    if (node->parent && scene_detect_loop(node->parent, node)) {
        snprintf(error_out, error_size, "Loop detected in node hierarchy");
        return false;
    }
    
    // Validate children recursively
    for (size_t i = 0; i < node->child_count; i++) {
        char child_error[256];
        if (!scene_node_validate(node->children[i], child_error, sizeof(child_error))) {
            snprintf(error_out, error_size, "Child %zu validation failed: %s", i, child_error);
            return false;
        }
    }
    
    node->validated = true;
    return true;
}

bool scene_validate(Scene* scene, char* error_out, size_t error_size) {
    if (!scene) {
        snprintf(error_out, error_size, "NULL scene");
        return false;
    }
    
    // Validate root
    if (!scene_node_validate(&scene->base, error_out, error_size)) {
        strncpy(scene->last_validation_error, error_out, sizeof(scene->last_validation_error) - 1);
        return false;
    }
    
    // Check workspace uniqueness across outputs
    for (size_t i = 0; i < scene->output_count; i++) {
        SceneOutput* output = scene->outputs[i];
        for (size_t j = 0; j < output->workspace_count; j++) {
            SceneWorkspace* ws = output->workspaces[j];
            
            // Verify workspace belongs to this output
            if (ws->output != output) {
                snprintf(error_out, error_size, 
                         "Workspace %u belongs to different output", ws->id);
                strncpy(scene->last_validation_error, error_out, sizeof(scene->last_validation_error) - 1);
                return false;
            }
            
            // Check for duplicate workspace IDs on this output
            for (size_t k = j + 1; k < output->workspace_count; k++) {
                if (output->workspaces[k]->id == ws->id) {
                    snprintf(error_out, error_size, 
                             "Duplicate workspace ID %u on output", ws->id);
                    strncpy(scene->last_validation_error, error_out, sizeof(scene->last_validation_error) - 1);
                    return false;
                }
            }
        }
    }
    
    LOG_DEBUG("[Scene] Validation passed (outputs=%zu, nodes=%lu)", 
              scene->output_count, (unsigned long)scene_count_nodes(scene, SCENE_NODE_ROOT));
    return true;
}

// ============================================================================
// Output Operations
// ============================================================================

SceneOutput* scene_output_create(Scene* scene, struct wlr_output* wlr_output) {
    if (!scene || !wlr_output) {
        LOG_ERROR("[Scene] NULL scene or wlr_output");
        return NULL;
    }
    
    SceneOutput* output = (SceneOutput*)calloc(1, sizeof(SceneOutput));
    if (!output) {
        LOG_ERROR("[Scene] Failed to allocate output");
        return NULL;
    }
    
    init_scene_node(&output->base, SCENE_NODE_OUTPUT, scene);
    output->wlr_output = wlr_output;
    output->wlr_scene_output = NULL;
    output->workspaces = NULL;
    output->workspace_count = 0;
    output->workspace_capacity = 0;
    output->active_workspace_id = 0;
    output->zoom = 1.0f;
    output->zoom_center_x = -1.0f;
    output->zoom_center_y = -1.0f;
    
    // Add to scene
    char error[256];
    if (!add_child_to_node(&scene->base, &output->base, error, sizeof(error))) {
        LOG_ERROR("[Scene] Failed to add output to scene: %s", error);
        free(output);
        return NULL;
    }
    
    // Grow outputs array
    if (scene->output_count >= scene->output_capacity) {
        size_t new_capacity = scene->output_capacity == 0 ? 4 : scene->output_capacity * 2;
        SceneOutput** new_outputs = (SceneOutput**)realloc(scene->outputs, new_capacity * sizeof(SceneOutput*));
        if (!new_outputs) {
            LOG_ERROR("[Scene] Failed to grow outputs array");
            scene_node_remove_child(&scene->base, &output->base, error, sizeof(error));
            free(output);
            return NULL;
        }
        scene->outputs = new_outputs;
        scene->output_capacity = new_capacity;
    }
    
    scene->outputs[scene->output_count++] = output;
    
    LOG_INFO("[Scene] Created output (id=%lu)", (unsigned long)output->base.id);
    return output;
}

void scene_output_destroy(SceneOutput* output) {
    if (!output) return;
    
    LOG_INFO("[Scene] Destroying output");
    
    // Destroy all workspaces
    for (size_t i = 0; i < output->workspace_count; i++) {
        scene_workspace_destroy(output->workspaces[i]);
    }
    
    free(output->workspaces);
    free(output->base.children);
    
    // Remove from scene
    Scene* scene = (Scene*)output->base.parent;
    if (scene) {
        for (size_t i = 0; i < scene->output_count; i++) {
            if (scene->outputs[i] == output) {
                for (size_t j = i + 1; j < scene->output_count; j++) {
                    scene->outputs[j - 1] = scene->outputs[j];
                }
                scene->output_count--;
                break;
            }
        }
    }
    
    free(output);
}

SceneWorkspace* scene_output_get_workspace(SceneOutput* output, uint32_t workspace_id) {
    if (!output) return NULL;
    
    // Find existing workspace
    for (size_t i = 0; i < output->workspace_count; i++) {
        if (output->workspaces[i]->id == workspace_id) {
            return output->workspaces[i];
        }
    }
    
    // Create new workspace
    SceneWorkspace* ws = scene_workspace_create(output, workspace_id);
    if (!ws) {
        LOG_ERROR("[Scene] Failed to create workspace %u", workspace_id);
        return NULL;
    }
    
    LOG_INFO("[Scene] Created workspace %u on output", workspace_id);
    return ws;
}

// ============================================================================
// Workspace Operations
// ============================================================================

SceneWorkspace* scene_workspace_create(SceneOutput* output, uint32_t id) {
    if (!output) return NULL;
    
    // Check for duplicate workspace ID on this output
    for (size_t i = 0; i < output->workspace_count; i++) {
        if (output->workspaces[i]->id == id) {
            LOG_ERROR("[Scene] Workspace %u already exists on output", id);
            return NULL;
        }
    }
    
    SceneWorkspace* ws = (SceneWorkspace*)calloc(1, sizeof(SceneWorkspace));
    if (!ws) {
        LOG_ERROR("[Scene] Failed to allocate workspace");
        return NULL;
    }
    
    Scene* scene = (Scene*)output->base.parent;
    init_scene_node(&ws->base, SCENE_NODE_WORKSPACE, scene);
    
    ws->id = id;
    snprintf(ws->name, sizeof(ws->name), "Workspace %u", id);
    ws->containers = NULL;
    ws->container_count = 0;
    ws->container_capacity = 0;
    ws->floating_views = NULL;
    ws->floating_count = 0;
    ws->floating_capacity = 0;
    ws->output = output;
    ws->active_container = NULL;
    ws->active_view = NULL;
    
    // Add to output
    char error[256];
    if (!add_child_to_node(&output->base, &ws->base, error, sizeof(error))) {
        LOG_ERROR("[Scene] Failed to add workspace to output: %s", error);
        free(ws);
        return NULL;
    }
    
    // Grow workspaces array
    if (output->workspace_count >= output->workspace_capacity) {
        size_t new_capacity = output->workspace_capacity == 0 ? 4 : output->workspace_capacity * 2;
        SceneWorkspace** new_workspaces = (SceneWorkspace**)realloc(output->workspaces, new_capacity * sizeof(SceneWorkspace*));
        if (!new_workspaces) {
            LOG_ERROR("[Scene] Failed to grow workspaces array");
            scene_node_remove_child(&output->base, &ws->base, error, sizeof(error));
            free(ws);
            return NULL;
        }
        output->workspaces = new_workspaces;
        output->workspace_capacity = new_capacity;
    }
    
    output->workspaces[output->workspace_count++] = ws;
    return ws;
}

void scene_workspace_destroy(SceneWorkspace* ws) {
    if (!ws) return;
    
    LOG_INFO("[Scene] Destroying workspace %u", ws->id);
    
    // Destroy all containers (which destroys their views)
    for (size_t i = 0; i < ws->container_count; i++) {
        scene_container_destroy(ws->containers[i]);
    }
    
    // Destroy floating views
    for (size_t i = 0; i < ws->floating_count; i++) {
        scene_view_destroy(ws->floating_views[i]);
    }
    
    free(ws->containers);
    free(ws->floating_views);
    free(ws->base.children);
    
    // Remove from output
    SceneOutput* output = ws->output;
    if (output) {
        for (size_t i = 0; i < output->workspace_count; i++) {
            if (output->workspaces[i] == ws) {
                for (size_t j = i + 1; j < output->workspace_count; j++) {
                    output->workspaces[j - 1] = output->workspaces[j];
                }
                output->workspace_count--;
                break;
            }
        }
    }
    
    free(ws);
}

// ============================================================================
// Debug/Introspection
// ============================================================================

char* scene_node_get_path(SceneNode* node, char* buffer, size_t buffer_size) {
    if (!node || !buffer) return NULL;
    
    buffer[0] = '\0';
    size_t pos = 0;
    
    // Build path from node to root
    SceneNode* current = node;
    char parts[32][64];
    size_t part_count = 0;
    
    while (current != NULL) {
        const char* type_str;
        switch (current->type) {
            case SCENE_NODE_ROOT: type_str = "Scene"; break;
            case SCENE_NODE_OUTPUT: type_str = "Output"; break;
            case SCENE_NODE_WORKSPACE: type_str = "Workspace"; break;
            case SCENE_NODE_CONTAINER: type_str = "Container"; break;
            case SCENE_NODE_VIEW: type_str = "View"; break;
            default: type_str = "Unknown"; break;
        }
        
        snprintf(parts[part_count], sizeof(parts[0]), "%s#%lu", type_str, (unsigned long)current->id);
        part_count++;
        
        if (part_count >= 32) break;
        current = current->parent;
    }
    
    // Build path string (root to node)
    for (size_t i = part_count; i > 0; i--) {
        size_t n = snprintf(buffer + pos, buffer_size - pos, "%s%s", 
                           (i < part_count) ? " → " : "", parts[i - 1]);
        pos += n;
        if (pos >= buffer_size - 1) break;
    }
    
    return buffer;
}

void scene_node_print_tree(SceneNode* node, int depth) {
    if (!node) return;
    
    const char* type_str;
    switch (node->type) {
        case SCENE_NODE_ROOT: type_str = "Scene"; break;
        case SCENE_NODE_OUTPUT: type_str = "Output"; break;
        case SCENE_NODE_WORKSPACE: type_str = "Workspace"; break;
        case SCENE_NODE_CONTAINER: type_str = "Container"; break;
        case SCENE_NODE_VIEW: type_str = "View"; break;
        default: type_str = "Unknown"; break;
    }
    
    char indent[64];
    for (int i = 0; i < depth && i < 63; i++) indent[i] = ' ';
    indent[depth < 63 ? depth : 63] = '\0';
    
    printf("%s%s#%lu [%dx%d+%d,%d]%s%s\n", 
           indent, type_str, (unsigned long)node->id,
           node->width, node->height, node->x, node->y,
           node->dirty ? " DIRTY" : "",
           node->validated ? "" : " UNVALIDATED");
    
    for (size_t i = 0; i < node->child_count; i++) {
        scene_node_print_tree(node->children[i], depth + 2);
    }
}

void scene_print_tree(Scene* scene) {
    if (!scene) {
        printf("[Scene] NULL scene\n");
        return;
    }
    
    printf("=== Scene Graph ===\n");
    scene_node_print_tree(&scene->base, 0);
    printf("===================\n");
}

size_t scene_count_nodes(Scene* scene, SceneNodeType type) {
    if (!scene) return 0;
    
    size_t count = 0;
    
    // Recursive helper
    struct { SceneNode* node; size_t index; } stack[256];
    size_t stack_top = 0;
    
    stack[stack_top].node = &scene->base;
    stack[stack_top].index = 0;
    
    while (1) {
        SceneNode* node = stack[stack_top].node;
        
        if (node->type == type) count++;
        
        if (stack[stack_top].index < node->child_count) {
            stack_top++;
            stack[stack_top].node = node->children[stack[stack_top - 1].index];
            stack[stack_top].index = 0;
            stack[stack_top - 1].index++;
        } else {
            if (stack_top == 0) break;
            stack_top--;
        }
        
        if (stack_top >= 255) break;
    }
    
    return count;
}

SceneNode* scene_find_node_by_id(Scene* scene, uint64_t id) {
    if (!scene) return NULL;
    
    // Simple BFS
    SceneNode* queue[1024];
    size_t head = 0, tail = 0;
    
    queue[tail++] = &scene->base;
    
    while (head < tail) {
        SceneNode* node = queue[head++];
        if (node->id == id) return node;
        
        for (size_t i = 0; i < node->child_count && tail < 1024; i++) {
            queue[tail++] = node->children[i];
        }
    }
    
    return NULL;
}
