// Debug patch for black screen issue
// Apply these changes to wlr_bridge.c

// FIX 1: Enable overlay layer by default (around line 1720)
// Change this:
//   wlr_scene_node_set_enabled(&server->overlay_layer->node, false);
// To this:
//   wlr_scene_node_set_enabled(&server->overlay_layer->node, true);  // ENABLE IT!

// FIX 2: Add debug logging in output_frame (around line 850)
// Add at start of output_frame function:
/*
    static int frame_count = 0;
    frame_count++;
    if (frame_count % 10 == 0) {
        LOG_INFO("[DEBUG] Frame %d on %s (enabled=%d, scene_output=%p)", 
                 frame_count, output->output->name, 
                 output->output->enabled, output->scene_output);
    }
*/

// FIX 3: Ensure outputs are enabled in server_new_output (around line 950)
// After wlr_output_state_set_enabled, add:
/*
    LOG_INFO("[OUTPUT] %s enabled=%d, mode=%dx%d@%dHz",
             wlr_output->name,
             wlr_output->enabled,
             mode->width, mode->height,
             mode->refresh);
*/

// FIX 4: Add visual test - create a colored rect to confirm rendering
// In server_new_output, after creating scene_output, add:
/*
    // Create a test rectangle to confirm rendering works
    struct wlr_scene_rect *test_rect = wlr_scene_rect_create(
        server->scene, 100, 100, (float[4]){1.0f, 0.0f, 0.0f, 1.0f});  // Red box
    wlr_scene_node_set_position(&test_rect->node, 50, 50);
    LOG_INFO("[TEST] Created red test box at (50,50) size 100x100");
*/

// FIX 5: Check if wlroots is actually rendering
// Add in output_frame, before wlr_scene_output_commit:
/*
    LOG_INFO("[FRAME] About to commit scene for %s", output->output->name);
    LOG_INFO("[FRAME]   scene_output->scene = %p", output->scene_output->scene);
    LOG_INFO("[FRAME]   scene->tree.enabled = %d", output->scene_output->scene->tree.node.enabled);
    LOG_INFO("[FRAME]   output->enabled = %d", output->output->enabled);
*/
