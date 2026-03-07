// Common Utilities - Reduce code duplication across the codebase

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Memory Utilities
// ============================================================================

/**
 * Safe calloc with error logging
 * Returns NULL on failure (same as calloc)
 */
static inline void* safe_calloc(size_t count, size_t size, const char* type_name) {
    void* ptr = calloc(count, size);
    if (!ptr && count > 0 && size > 0) {
        // Would log error here if logger available
        (void)type_name;  // Suppress unused warning in release
    }
    return ptr;
}

/**
 * Safe malloc with error logging
 */
static inline void* safe_malloc(size_t size, const char* type_name) {
    void* ptr = malloc(size);
    if (!ptr && size > 0) {
        (void)type_name;
    }
    return ptr;
}

/**
 * Safe realloc with error logging
 */
static inline void* safe_realloc(void* ptr, size_t size, const char* type_name) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        (void)type_name;
    }
    return new_ptr;
}

/**
 * Free and set pointer to NULL
 */
static inline void safe_free(void** ptr) {
    if (ptr && *ptr) {
        free(*ptr);
        *ptr = NULL;
    }
}

// Macro wrappers for cleaner code
#define CALLOC_T(type, count) ((type*)safe_calloc(count, sizeof(type), #type))
#define MALLOC_T(type) ((type*)safe_malloc(sizeof(type), #type))
#define REALLOC_T(ptr, type, count) ((type*)safe_realloc(ptr, (count) * sizeof(type), #type))
#define FREE(ptr) safe_free((void**)&(ptr))

// ============================================================================
// Array/Vector Utilities
// ============================================================================

/**
 * Dynamic array header (store before array data)
 */
typedef struct {
    size_t size;
    size_t capacity;
} ArrayHeader;

/**
 * Get array header from data pointer
 */
static inline ArrayHeader* array_header(void* arr) {
    return (ArrayHeader*)((char*)arr - sizeof(ArrayHeader));
}

/**
 * Get array size
 */
static inline size_t array_size(void* arr) {
    return arr ? array_header(arr)->size : 0;
}

/**
 * Get array capacity
 */
static inline size_t array_capacity(void* arr) {
    return arr ? array_header(arr)->capacity : 0;
}

/**
 * Create new dynamic array
 */
static inline void* array_create(size_t element_size, size_t initial_capacity) {
    ArrayHeader* header = (ArrayHeader*)malloc(sizeof(ArrayHeader) + element_size * initial_capacity);
    if (!header) return NULL;
    
    header->size = 0;
    header->capacity = initial_capacity;
    return (char*)header + sizeof(ArrayHeader);
}

/**
 * Ensure array has capacity for n more elements
 */
static inline void* array_ensure_capacity(void* arr, size_t element_size, size_t additional) {
    if (!arr) return NULL;
    
    ArrayHeader* header = array_header(arr);
    size_t needed = header->size + additional;
    
    if (needed <= header->capacity) return arr;
    
    size_t new_capacity = header->capacity * 2;
    while (new_capacity < needed) new_capacity *= 2;
    
    ArrayHeader* new_header = (ArrayHeader*)realloc(header, 
        sizeof(ArrayHeader) + element_size * new_capacity);
    if (!new_header) return NULL;
    
    new_header->capacity = new_capacity;
    return (char*)new_header + sizeof(ArrayHeader);
}

/**
 * Push element to array
 */
static inline void* array_push(void* arr, size_t element_size, const void* element) {
    arr = array_ensure_capacity(arr, element_size, 1);
    if (!arr) return NULL;
    
    ArrayHeader* header = array_header(arr);
    memcpy((char*)arr + header->size * element_size, element, element_size);
    header->size++;
    
    return arr;
}

/**
 * Pop element from array
 */
static inline bool array_pop(void* arr, size_t element_size, void* out_element) {
    if (!arr) return false;
    
    ArrayHeader* header = array_header(arr);
    if (header->size == 0) return false;
    
    header->size--;
    if (out_element) {
        memcpy(out_element, (char*)arr + header->size * element_size, element_size);
    }
    
    return true;
}

/**
 * Clear array (keep capacity)
 */
static inline void array_clear(void* arr) {
    if (arr) {
        array_header(arr)->size = 0;
    }
}

/**
 * Free array
 */
static inline void array_free(void* arr) {
    if (arr) {
        free(array_header(arr));
    }
}

// Macro wrappers for type-safe array operations
#define ARRAY_CREATE(type) ((type*)array_create(sizeof(type), 8))
#define ARRAY_PUSH(arr, elem) do { arr = array_push(arr, sizeof(*(arr)), elem); } while(0)
#define ARRAY_POP(arr, out) array_pop(arr, sizeof(*(arr)), out)
#define ARRAY_SIZE(arr) array_size(arr)
#define ARRAY_FREE(arr) do { array_free(arr); arr = NULL; } while(0)
#define ARRAY_CLEAR(arr) array_clear(arr)

// ============================================================================
// String Utilities
// ============================================================================

/**
 * Duplicate string
 */
static inline char* str_dup(const char* str) {
    if (!str) return NULL;
    
    size_t len = strlen(str) + 1;
    char* dup = (char*)malloc(len);
    if (dup) {
        memcpy(dup, str, len);
    }
    return dup;
}

/**
 * Concatenate two strings
 */
static inline char* str_concat(const char* a, const char* b) {
    if (!a && !b) return NULL;
    if (!a) return str_dup(b);
    if (!b) return str_dup(a);
    
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    char* result = (char*)malloc(len_a + len_b + 1);
    
    if (result) {
        memcpy(result, a, len_a);
        memcpy(result + len_a, b, len_b + 1);
    }
    
    return result;
}

/**
 * Check if string starts with prefix
 */
static inline bool str_starts_with(const char* str, const char* prefix) {
    if (!str || !prefix) return false;
    
    size_t prefix_len = strlen(prefix);
    return strncmp(str, prefix, prefix_len) == 0;
}

/**
 * Check if string ends with suffix
 */
static inline bool str_ends_with(const char* str, const char* suffix) {
    if (!str || !suffix) return false;
    
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    
    if (suffix_len > str_len) return false;
    
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

/**
 * Case-insensitive string comparison
 */
static inline int strcasecmp_custom(const char* a, const char* b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        
        // Convert to lowercase
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        
        if (ca != cb) return ca - cb;
        
        a++;
        b++;
    }
    
    return *a - *b;
}

/**
 * Trim whitespace from string (in-place)
 */
static inline char* str_trim(char* str) {
    if (!str) return NULL;
    
    // Trim leading
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }
    
    if (*str == '\0') return str;
    
    // Trim trailing
    char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        end--;
    }
    *(end + 1) = '\0';
    
    return str;
}

// ============================================================================
// Time Utilities
// ============================================================================

/**
 * Get current time in milliseconds
 */
static inline uint64_t time_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/**
 * Get current time in microseconds
 */
static inline uint64_t time_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/**
 * Get delta time in milliseconds
 */
static inline uint64_t time_delta_ms(uint64_t start_ms) {
    uint64_t now = time_now_ms();
    return (now > start_ms) ? (now - start_ms) : 0;
}

/**
 * Clamp value between min and max
 */
static inline int64_t clamp_int(int64_t value, int64_t min, int64_t max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static inline float clamp_float(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

// ============================================================================
// Math Utilities
// ============================================================================

/**
 * Linear interpolation
 */
static inline float lerp(float a, float b, float t) {
    return a + (b - a) * clamp_float(t, 0.0f, 1.0f);
}

/**
 * Smooth step interpolation
 */
static inline float smoothstep(float edge0, float edge1, float x) {
    float t = clamp_float((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/**
 * Map value from one range to another
 */
static inline float map_range(float value, float in_min, float in_max, float out_min, float out_max) {
    return (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/**
 * Degrees to radians
 */
static inline float deg_to_rad(float degrees) {
    return degrees * 0.017453292519943295f;  // PI / 180
}

/**
 * Radians to degrees
 */
static inline float rad_to_deg(float radians) {
    return radians * 57.29577951308232f;  // 180 / PI
}

// ============================================================================
// Rectangle Utilities
// ============================================================================

typedef struct {
    int x, y;
    int width, height;
} Rect;

/**
 * Check if point is inside rectangle
 */
static inline bool rect_contains(const Rect* rect, int x, int y) {
    return rect && 
           x >= rect->x && x < rect->x + rect->width &&
           y >= rect->y && y < rect->y + rect->height;
}

/**
 * Check if two rectangles intersect
 */
static inline bool rect_intersects(const Rect* a, const Rect* b) {
    if (!a || !b) return false;
    
    return !(a->x + a->width <= b->x ||
             b->x + b->width <= a->x ||
             a->y + a->height <= b->y ||
             b->y + b->height <= a->y);
}

/**
 * Get intersection of two rectangles
 */
static inline Rect rect_intersection(const Rect* a, const Rect* b) {
    Rect result = {0, 0, 0, 0};
    
    if (!a || !b || !rect_intersects(a, b)) {
        return result;
    }
    
    result.x = (a->x > b->x) ? a->x : b->x;
    result.y = (a->y > b->y) ? a->y : b->y;
    result.width = ((a->x + a->width) < (b->x + b->width) ? (a->x + a->width) : (b->x + b->width)) - result.x;
    result.height = ((a->y + a->height) < (b->y + b->height) ? (a->y + a->height) : (b->y + b->height)) - result.y;
    
    return result;
}

/**
 * Union of two rectangles
 */
static inline Rect rect_union(const Rect* a, const Rect* b) {
    Rect result = {0, 0, 0, 0};
    
    if (!a && !b) return result;
    if (!a) return *b;
    if (!b) return *a;
    
    result.x = (a->x < b->x) ? a->x : b->x;
    result.y = (a->y < b->y) ? a->y : b->y;
    result.width = ((a->x + a->width) > (b->x + b->width) ? (a->x + a->width) : (b->x + b->width)) - result.x;
    result.height = ((a->y + a->height) > (b->y + b->height) ? (a->y + a->height) : (b->y + b->height)) - result.y;
    
    return result;
}

/**
 * Check if two rectangles are equal
 */
static inline bool rect_equals(const Rect* a, const Rect* b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    
    return a->x == b->x && a->y == b->y && 
           a->width == b->width && a->height == b->height;
}

// ============================================================================
// Bit Utilities
// ============================================================================

/**
 * Check if bit is set
 */
static inline bool bit_is_set(uint32_t value, uint32_t bit) {
    return (value & (1u << bit)) != 0;
}

/**
 * Set bit
 */
static inline uint32_t bit_set(uint32_t value, uint32_t bit) {
    return value | (1u << bit);
}

/**
 * Clear bit
 */
static inline uint32_t bit_clear(uint32_t value, uint32_t bit) {
    return value & ~(1u << bit);
}

/**
 * Toggle bit
 */
static inline uint32_t bit_toggle(uint32_t value, uint32_t bit) {
    return value ^ (1u << bit);
}

/**
 * Count set bits (population count)
 */
static inline uint32_t bit_count(uint32_t value) {
    uint32_t count = 0;
    while (value) {
        count += value & 1;
        value >>= 1;
    }
    return count;
}

/**
 * Get next power of 2
 */
static inline uint32_t next_power_of_2(uint32_t value) {
    if (value == 0) return 1;
    
    value--;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value++;
    
    return value;
}

/**
 * Check if value is power of 2
 */
static inline bool is_power_of_2(uint32_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

#ifdef __cplusplus
}
#endif
