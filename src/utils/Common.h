// Common Utilities - Reduce code duplication across the codebase

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <math.h>

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

// ============================================================================
// Color Utilities
// ============================================================================

typedef struct {
    float r, g, b, a;
} Color;

/**
 * Create color from RGBA values (0-1)
 */
static inline Color color_from_rgba(float r, float g, float b, float a) {
    Color c = {r, g, b, a};
    return c;
}

/**
 * Create color from RGB values (0-1), alpha = 1.0
 */
static inline Color color_from_rgb(float r, float g, float b) {
    return color_from_rgba(r, g, b, 1.0f);
}

/**
 * Create color from 0-255 RGBA values
 */
static inline Color color_from_rgba_255(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return color_from_rgba(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

/**
 * Create color from hex string (#RRGGBB or #RRGGBBAA)
 */
static inline Color color_from_hex(const char* hex) {
    Color c = {1.0f, 1.0f, 1.0f, 1.0f};
    
    if (!hex || hex[0] != '#') return c;
    
    hex++;  // Skip '#'
    
    unsigned int r, g, b, a = 255;
    if (strlen(hex) == 6) {
        sscanf(hex, "%2x%2x%2x", &r, &g, &b);
    } else if (strlen(hex) == 8) {
        sscanf(hex, "%2x%2x%2x%2x", &r, &g, &b, &a);
    }
    
    c.r = r / 255.0f;
    c.g = g / 255.0f;
    c.b = b / 255.0f;
    c.a = a / 255.0f;
    
    return c;
}

/**
 * Convert color to 32-bit RGBA
 */
static inline uint32_t color_to_rgba32(const Color* c) {
    if (!c) return 0xFFFFFFFF;
    
    uint8_t r = (uint8_t)(c->r * 255.0f);
    uint8_t g = (uint8_t)(c->g * 255.0f);
    uint8_t b = (uint8_t)(c->b * 255.0f);
    uint8_t a = (uint8_t)(c->a * 255.0f);
    
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/**
 * Linear interpolation between colors
 */
static inline Color color_lerp(const Color* a, const Color* b, float t) {
    Color result;
    t = clamp_float(t, 0.0f, 1.0f);
    
    result.r = lerp(a->r, b->r, t);
    result.g = lerp(a->g, b->g, t);
    result.b = lerp(a->b, b->b, t);
    result.a = lerp(a->a, b->a, t);
    
    return result;
}

/**
 * Check if two colors are equal (within epsilon)
 */
static inline bool color_equals(const Color* a, const Color* b, float epsilon) {
    if (!a || !b) return false;
    
    return fabsf(a->r - b->r) < epsilon &&
           fabsf(a->g - b->g) < epsilon &&
           fabsf(a->b - b->b) < epsilon &&
           fabsf(a->a - b->a) < epsilon;
}

// ============================================================================
// File I/O Utilities
// ============================================================================

/**
 * Read entire file into buffer
 * Returns NULL on failure. Caller must free() the result.
 */
static inline char* file_read_all(const char* path, size_t* out_size) {
    if (!path) return NULL;
    
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    
    // Allocate buffer
    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    
    // Read file
    size_t read_size = fread(buffer, 1, size, file);
    fclose(file);
    
    buffer[read_size] = '\0';
    
    if (out_size) *out_size = read_size;
    return buffer;
}

/**
 * Write buffer to file
 * Returns true on success
 */
static inline bool file_write_all(const char* path, const void* data, size_t size) {
    if (!path || !data) return false;
    
    FILE* file = fopen(path, "wb");
    if (!file) return false;
    
    size_t written = fwrite(data, 1, size, file);
    fclose(file);
    
    return written == size;
}

/**
 * Check if file exists
 */
static inline bool file_exists(const char* path) {
    if (!path) return false;
    
    FILE* file = fopen(path, "r");
    if (file) {
        fclose(file);
        return true;
    }
    return false;
}

/**
 * Get file size
 */
static inline long file_size(const char* path) {
    if (!path) return -1;
    
    FILE* file = fopen(path, "rb");
    if (!file) return -1;
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    
    return size;
}

// ============================================================================
// Logging Helpers (work with Logger.h)
// ============================================================================

/**
 * Print hex dump of buffer
 */
static inline void hex_dump(const void* data, size_t size, const char* label) {
    if (!data || size == 0) return;
    
    const uint8_t* bytes = (const uint8_t*)data;
    
    if (label) {
        printf("%s (%zu bytes):\n", label, size);
    }
    
    for (size_t i = 0; i < size; i += 16) {
        printf("%04zx: ", i);
        
        // Print hex values
        for (size_t j = 0; j < 16 && i + j < size; j++) {
            printf("%02x ", bytes[i + j]);
        }
        
        // Print ASCII
        printf("  ");
        for (size_t j = 0; j < 16 && i + j < size; j++) {
            char c = bytes[i + j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        
        printf("\n");
    }
}

/**
 * Print array of integers
 */
static inline void print_int_array(const int* arr, size_t size, const char* label) {
    if (!arr || size == 0) return;
    
    if (label) printf("%s: ", label);
    printf("[");
    
    for (size_t i = 0; i < size; i++) {
        printf("%d", arr[i]);
        if (i < size - 1) printf(", ");
    }
    
    printf("]\n");
}

// ============================================================================
// Linked List Utilities
// ============================================================================

/**
 * Generic linked list node
 */
typedef struct ListNode {
    struct ListNode* next;
    struct ListNode* prev;
} ListNode;

/**
 * Initialize list node
 */
static inline void list_node_init(ListNode* node) {
    if (node) {
        node->next = node;
        node->prev = node;
    }
}

/**
 * Add node after another node
 */
static inline void list_add_after(ListNode* after, ListNode* node) {
    if (!after || !node) return;
    
    node->prev = after;
    node->next = after->next;
    after->next->prev = node;
    after->next = node;
}

/**
 * Add node before another node
 */
static inline void list_add_before(ListNode* before, ListNode* node) {
    if (!before || !node) return;
    
    list_add_after(before->prev, node);
}

/**
 * Remove node from list
 */
static inline void list_remove(ListNode* node) {
    if (!node) return;
    
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

/**
 * Check if list is empty
 */
static inline bool list_is_empty(const ListNode* head) {
    return !head || head->next == head;
}

/**
 * Get next element (with wraparound)
 */
static inline ListNode* list_next(const ListNode* node) {
    return node ? node->next : NULL;
}

/**
 * Get previous element (with wraparound)
 */
static inline ListNode* list_prev(const ListNode* node) {
    return node ? node->prev : NULL;
}

// ============================================================================
// Hash Utilities
// ============================================================================

/**
 * Simple hash function (djb2)
 */
static inline uint32_t hash_string(const char* str) {
    if (!str) return 0;
    
    uint32_t hash = 5381;
    int c;
    
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    
    return hash;
}

/**
 * Hash integer
 */
static inline uint32_t hash_int(int value) {
    value = ((value >> 16) ^ value) * 0x45d9f3b;
    value = ((value >> 16) ^ value) * 0x85ebca6b;
    value = (value >> 16) ^ value;
    return value;
}

/**
 * Hash pointer
 */
static inline uint32_t hash_ptr(const void* ptr) {
    return hash_int((int)(uintptr_t)ptr);
}

// ============================================================================
// Timer/Stopwatch Utilities
// ============================================================================

typedef struct {
    uint64_t start_ms;
    uint64_t elapsed_ms;
    bool running;
} Timer;

/**
 * Create and start timer
 */
static inline Timer timer_start(void) {
    Timer t;
    t.start_ms = time_now_ms();
    t.elapsed_ms = 0;
    t.running = true;
    return t;
}

/**
 * Stop timer
 */
static inline void timer_stop(Timer* t) {
    if (t && t->running) {
        t->elapsed_ms = time_delta_ms(t->start_ms);
        t->running = false;
    }
}

/**
 * Get elapsed time in ms
 */
static inline uint64_t timer_elapsed_ms(const Timer* t) {
    if (!t) return 0;
    if (t->running) return time_delta_ms(t->start_ms);
    return t->elapsed_ms;
}

/**
 * Reset and restart timer
 */
static inline void timer_restart(Timer* t) {
    if (t) {
        t->start_ms = time_now_ms();
        t->elapsed_ms = 0;
        t->running = true;
    }
}

// ============================================================================
// Callback/Function Pointer Utilities
// ============================================================================

/**
 * Generic callback with user data
 */
typedef void (*Callback)(void* user_data);

/**
 * Callback with integer argument
 */
typedef void (*CallbackInt)(int value, void* user_data);

/**
 * Callback with float argument
 */
typedef void (*CallbackFloat)(float value, void* user_data);

/**
 * Callback with two integers
 */
typedef void (*CallbackIntInt)(int a, int b, void* user_data);

/**
 * Safe callback invocation
 */
static inline void callback_invoke(Callback cb, void* user_data) {
    if (cb) cb(user_data);
}

static inline void callback_invoke_int(CallbackInt cb, int value, void* user_data) {
    if (cb) cb(value, user_data);
}

static inline void callback_invoke_float(CallbackFloat cb, float value, void* user_data) {
    if (cb) cb(value, user_data);
}

static inline void callback_invoke_int_int(CallbackIntInt cb, int a, int b, void* user_data) {
    if (cb) cb(a, b, user_data);
}

// ============================================================================
// Comparison/Sorting Utilities
// ============================================================================

/**
 * Compare integers (for qsort)
 */
static inline int compare_int(const void* a, const void* b) {
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    return (ia > ib) - (ia < ib);
}

/**
 * Compare floats (for qsort)
 */
static inline int compare_float(const void* a, const void* b) {
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    return (fa > fb) - (fa < fb);
}

/**
 * Compare strings (for qsort)
 */
static inline int compare_string(const void* a, const void* b) {
    const char* sa = *(const char**)a;
    const char* sb = *(const char**)b;
    return strcmp(sa, sb);
}

/**
 * Swap two integers
 */
static inline void swap_int(int* a, int* b) {
    if (a && b) {
        int temp = *a;
        *a = *b;
        *b = temp;
    }
}

/**
 * Swap two floats
 */
static inline void swap_float(float* a, float* b) {
    if (a && b) {
        float temp = *a;
        *a = *b;
        *b = temp;
    }
}

/**
 * Swap two pointers
 */
static inline void swap_ptr(void** a, void** b) {
    if (a && b) {
        void* temp = *a;
        *a = *b;
        *b = temp;
    }
}

// ============================================================================
// Min/Max Utilities
// ============================================================================

static inline int min_int(int a, int b) { return (a < b) ? a : b; }
static inline int max_int(int a, int b) { return (a > b) ? a : b; }
static inline float min_float(float a, float b) { return (a < b) ? a : b; }
static inline float max_float(float a, float b) { return (a > b) ? a : b; }
static inline uint32_t min_uint32(uint32_t a, uint32_t b) { return (a < b) ? a : b; }
static inline uint32_t max_uint32(uint32_t a, uint32_t b) { return (a > b) ? a : b; }

// ============================================================================
// Common Constants
// ============================================================================

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923f
#endif

#ifndef M_1_PI
#define M_1_PI 0.31830988618379067154f
#endif

#ifndef EPSILON
#define EPSILON 0.00001f
#endif

#ifndef EPSILON_DOUBLE
#define EPSILON_DOUBLE 0.0000001
#endif

// Array size macro
#define ARRAY_LENGTH(arr) (sizeof(arr) / sizeof((arr)[0]))

// Offset of member in struct
#define OFFSET_OF(type, member) ((size_t)&((type*)0)->member)

// Container of macro (get struct from member pointer)
#define CONTAINER_OF(ptr, type, member) \
    ((type*)((char*)(ptr) - OFFSET_OF(type, member)))

// Swap macro
#define SWAP(a, b) do { \
    __typeof__(a) _temp = (a); \
    (a) = (b); \
    (b) = _temp; \
} while(0)

// Min/Max macros
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(x, lo, hi) MIN(MAX(x, lo), hi)
#define ABS(x) (((x) < 0) ? -(x) : (x))
#define SIGN(x) (((x) < 0) ? -1 : (((x) > 0) ? 1 : 0))

// Unused parameter macro
#define UNUSED(x) (void)(x)

// Likely/unlikely hints for branch prediction
#ifdef __GNUC__
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

#ifdef __cplusplus
}
#endif
