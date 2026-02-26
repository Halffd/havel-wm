#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// View transform for scale/overview effects
typedef struct havel_view_transform {
    float scale_x;
    float scale_y;
    float translation_x;
    float translation_y;
    float alpha;
    bool active;
} havel_view_transform_t;

// Apply transform to view (scene graph transformation)
void havel_cpp_apply_view_transform(void* view, const havel_view_transform_t* transform);

// Remove transform from view
void havel_cpp_remove_view_transform(void* view);

// Get current transform
havel_view_transform_t havel_cpp_get_view_transform(void* view);

#ifdef __cplusplus
}
#endif
