// Damage Tracking - Efficient partial screen updates

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Damage region (rectangle)
typedef struct {
    int x, y;
    int width, height;
} DamageRegion;

// Damage tracker handle
typedef struct DamageTracker DamageTracker;

// Create/destroy damage tracker
DamageTracker* damage_tracker_create(int screen_width, int screen_height);
void damage_tracker_destroy(DamageTracker* tracker);

// Add damage region
void damage_tracker_add_damage(DamageTracker* tracker, int x, int y, int width, int height);

// Add full screen damage
void damage_tracker_add_full_damage(DamageTracker* tracker);

// Check if full redraw is needed
bool damage_tracker_needs_full_redraw(DamageTracker* tracker);

// Get damage regions (returns array, count stored in out_count)
const DamageRegion* damage_tracker_get_regions(DamageTracker* tracker, uint32_t* out_count);

// Clear damage (call after rendering)
void damage_tracker_clear(DamageTracker* tracker);

// Merge overlapping damage regions (optimization)
void damage_tracker_merge_regions(DamageTracker* tracker);

// Get total damaged area in pixels
uint32_t damage_tracker_get_damaged_area(DamageTracker* tracker);

// Get damage age (number of frames since last full redraw)
uint32_t damage_tracker_get_age(DamageTracker* tracker);

// Configure damage tracker
void damage_tracker_set_max_age(DamageTracker* tracker, uint32_t max_age);
void damage_tracker_set_merge_threshold(DamageTracker* tracker, float threshold);

// Statistics
typedef struct {
    uint32_t regionCount;
    uint32_t damagedArea;
    uint32_t totalArea;
    float damageRatio;  // damagedArea / totalArea
    uint32_t framesSinceFullRedraw;
    bool isFullRedraw;
} DamageStats;

void damage_tracker_get_stats(DamageTracker* tracker, DamageStats* stats);

#ifdef __cplusplus
}
#endif
