// Damage Tracking Implementation - Efficient partial screen updates

#include "DamageTracker.h"
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_DAMAGE_REGIONS 64
#define DEFAULT_MAX_AGE 60  // Frames before forced full redraw
#define MERGE_THRESHOLD 0.5f  // Merge if overlap ratio > 50%

// Internal damage tracker structure
struct DamageTracker {
    int screenWidth;
    int screenHeight;
    uint32_t totalArea;
    
    DamageRegion regions[MAX_DAMAGE_REGIONS];
    uint32_t regionCount;
    
    DamageRegion mergedRegions[MAX_DAMAGE_REGIONS];
    uint32_t mergedCount;
    
    bool needsFullRedraw;
    uint32_t maxAge;
    uint32_t currentAge;
    float mergeThreshold;
    
    DamageStats stats;
    struct timespec lastFullRedraw;
};

DamageTracker* damage_tracker_create(int screen_width, int screen_height) {
    if (screen_width <= 0 || screen_height <= 0) {
        LOG_ERROR("[DamageTracker] Invalid screen size: %dx%d", screen_width, screen_height);
        return NULL;
    }
    
    DamageTracker* tracker = (DamageTracker*)calloc(1, sizeof(DamageTracker));
    if (!tracker) {
        return NULL;
    }
    
    tracker->screenWidth = screen_width;
    tracker->screenHeight = screen_height;
    tracker->totalArea = screen_width * screen_height;
    tracker->regionCount = 0;
    tracker->mergedCount = 0;
    tracker->needsFullRedraw = true;  // First frame always full redraw
    tracker->maxAge = DEFAULT_MAX_AGE;
    tracker->currentAge = 0;
    tracker->mergeThreshold = MERGE_THRESHOLD;
    
    clock_gettime(CLOCK_MONOTONIC, &tracker->lastFullRedraw);
    
    memset(&tracker->stats, 0, sizeof(DamageStats));
    tracker->stats.totalArea = tracker->totalArea;
    tracker->stats.isFullRedraw = true;
    
    LOG_INFO("[DamageTracker] Created for %dx%d screen", screen_width, screen_height);
    return tracker;
}

void damage_tracker_destroy(DamageTracker* tracker) {
    if (!tracker) return;
    LOG_INFO("[DamageTracker] Destroyed");
    free(tracker);
}

void damage_tracker_add_damage(DamageTracker* tracker, int x, int y, int width, int height) {
    if (!tracker || width <= 0 || height <= 0) return;
    
    // Clamp to screen bounds
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x + width > tracker->screenWidth) {
        width = tracker->screenWidth - x;
    }
    if (y + height > tracker->screenHeight) {
        height = tracker->screenHeight - y;
    }
    
    if (width <= 0 || height <= 0) return;
    
    // Check if we've hit max regions
    if (tracker->regionCount >= MAX_DAMAGE_REGIONS) {
        // Too many regions, just do full redraw
        tracker->needsFullRedraw = true;
        LOG_DEBUG("[DamageTracker] Max regions reached, forcing full redraw");
        return;
    }
    
    // Add damage region
    DamageRegion* region = &tracker->regions[tracker->regionCount++];
    region->x = x;
    region->y = y;
    region->width = width;
    region->height = height;
    
    LOG_DEBUG("[DamageTracker] Added damage: (%d,%d) %dx%d", x, y, width, height);
}

void damage_tracker_add_full_damage(DamageTracker* tracker) {
    if (!tracker) return;
    
    tracker->needsFullRedraw = true;
    tracker->currentAge = 0;
    clock_gettime(CLOCK_MONOTONIC, &tracker->lastFullRedraw);
    
    LOG_DEBUG("[DamageTracker] Full damage requested");
}

bool damage_tracker_needs_full_redraw(DamageTracker* tracker) {
    if (!tracker) return true;
    return tracker->needsFullRedraw || tracker->currentAge >= tracker->maxAge;
}

// Check if two regions overlap
static bool regions_overlap(const DamageRegion* a, const DamageRegion* b) {
    return !(a->x + a->width <= b->x ||
             b->x + b->width <= a->x ||
             a->y + a->height <= b->y ||
             b->y + b->height <= a->y);
}

// Calculate overlap area between two regions
static uint32_t get_overlap_area(const DamageRegion* a, const DamageRegion* b) {
    if (!regions_overlap(a, b)) return 0;
    
    int overlap_x = (a->x > b->x ? a->x : b->x);
    int overlap_y = (a->y > b->y ? a->y : b->y);
    int overlap_w = (a->x + a->width < b->x + b->width ? a->x + a->width : b->x + b->width) - overlap_x;
    int overlap_h = (a->y + a->height < b->y + b->height ? a->y + a->height : b->y + b->height) - overlap_y;
    
    return overlap_w > 0 && overlap_h > 0 ? overlap_w * overlap_h : 0;
}

// Merge two regions into bounding box
static void merge_regions(DamageRegion* a, const DamageRegion* b, DamageRegion* result) {
    result->x = a->x < b->x ? a->x : b->x;
    result->y = a->y < b->y ? a->y : b->y;
    result->width = (a->x + a->width > b->x + b->width ? a->x + a->width : b->x + b->width) - result->x;
    result->height = (a->y + a->height > b->y + b->height ? a->y + a->height : b->y + b->height) - result->y;
}

void damage_tracker_merge_regions(DamageTracker* tracker) {
    if (!tracker || tracker->regionCount <= 1) return;
    
    // Simple O(n²) merge - good enough for small region counts
    bool merged[MAX_DAMAGE_REGIONS] = {false};
    tracker->mergedCount = 0;
    
    for (uint32_t i = 0; i < tracker->regionCount; i++) {
        if (merged[i]) continue;
        
        DamageRegion current = tracker->regions[i];
        merged[i] = true;
        
        // Try to merge with other regions
        for (uint32_t j = i + 1; j < tracker->regionCount; j++) {
            if (merged[j]) continue;
            
            uint32_t overlap = get_overlap_area(&current, &tracker->regions[j]);
            uint32_t smaller_area = (tracker->regions[j].width * tracker->regions[j].height);
            
            if (smaller_area > 0) {
                float overlap_ratio = (float)overlap / smaller_area;
                
                if (overlap_ratio >= tracker->mergeThreshold) {
                    DamageRegion merged_region;
                    merge_regions(&current, &tracker->regions[j], &merged_region);
                    current = merged_region;
                    merged[j] = true;
                }
            }
        }
        
        tracker->mergedRegions[tracker->mergedCount++] = current;
    }
    
    LOG_DEBUG("[DamageTracker] Merged %u regions to %u", tracker->regionCount, tracker->mergedCount);
}

const DamageRegion* damage_tracker_get_regions(DamageTracker* tracker, uint32_t* out_count) {
    if (!tracker || !out_count) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    
    // Merge regions for efficiency
    damage_tracker_merge_regions(tracker);
    
    *out_count = tracker->mergedCount;
    return tracker->mergedCount > 0 ? tracker->mergedRegions : tracker->regions;
}

void damage_tracker_clear(DamageTracker* tracker) {
    if (!tracker) return;
    
    tracker->regionCount = 0;
    tracker->mergedCount = 0;
    tracker->currentAge++;
    
    // Check if we need a full redraw due to age
    if (tracker->currentAge >= tracker->maxAge) {
        tracker->needsFullRedraw = true;
    }
}

uint32_t damage_tracker_get_damaged_area(DamageTracker* tracker) {
    if (!tracker) return 0;
    
    uint32_t total_damaged = 0;
    
    for (uint32_t i = 0; i < tracker->mergedCount; i++) {
        total_damaged += tracker->mergedRegions[i].width * tracker->mergedRegions[i].height;
    }
    
    return total_damaged;
}

uint32_t damage_tracker_get_age(DamageTracker* tracker) {
    return tracker ? tracker->currentAge : 0;
}

void damage_tracker_set_max_age(DamageTracker* tracker, uint32_t max_age) {
    if (!tracker) return;
    tracker->maxAge = max_age > 0 ? max_age : 1;
    LOG_INFO("[DamageTracker] Max age set to %u frames", tracker->maxAge);
}

void damage_tracker_set_merge_threshold(DamageTracker* tracker, float threshold) {
    if (!tracker) return;
    tracker->mergeThreshold = threshold < 0.0f ? 0.0f : (threshold > 1.0f ? 1.0f : threshold);
    LOG_INFO("[DamageTracker] Merge threshold set to %.2f", tracker->mergeThreshold);
}

void damage_tracker_get_stats(DamageTracker* tracker, DamageStats* stats) {
    if (!tracker || !stats) return;
    
    stats->regionCount = tracker->mergedCount;
    stats->damagedArea = damage_tracker_get_damaged_area(tracker);
    stats->totalArea = tracker->totalArea;
    stats->damageRatio = tracker->totalArea > 0 ? 
        (float)stats->damagedArea / tracker->totalArea : 1.0f;
    stats->framesSinceFullRedraw = tracker->currentAge;
    stats->isFullRedraw = tracker->needsFullRedraw;
}
