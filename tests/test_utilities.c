// Tests for Common.h Utilities

#include <utils/Common.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    tests_run++; \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAILED: %s\n", msg); \
        tests_failed++; \
        tests_passed--; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b), "Expected equality")
#define ASSERT_FLOAT_EQ(a, b, eps) ASSERT(fabsf((a) - (b)) < (eps), "Float equality failed")

// ============================================================================
// Memory Tests
// ============================================================================

TEST(memory_alloc) {
    int* arr = CALLOC_T(int, 10);
    ASSERT(arr != NULL, "CALLOC_T failed");
    
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(arr[i], 0);
    }
    
    FREE(arr);
    ASSERT(arr == NULL, "FREE didn't set to NULL");
}

TEST(memory_malloc) {
    int* val = MALLOC_T(int);
    ASSERT(val != NULL, "MALLOC_T failed");
    
    *val = 42;
    ASSERT_EQ(*val, 42);
    
    FREE(val);
}

TEST(memory_realloc) {
    int* arr = MALLOC_T(int);
    ASSERT(arr != NULL, "MALLOC_T failed");
    *arr = 1;
    
    arr = REALLOC_T(arr, int, 5);
    ASSERT(arr != NULL, "REALLOC_T failed");
    ASSERT_EQ(arr[0], 1);
    
    FREE(arr);
}

// ============================================================================
// Array Tests
// ============================================================================

TEST(array_basic) {
    int* arr = ARRAY_CREATE(int);
    ASSERT(arr != NULL, "ARRAY_CREATE failed");
    ASSERT_EQ(ARRAY_SIZE(arr), 0);
    
    int val1 = 1, val2 = 2, val3 = 3;
    ARRAY_PUSH(arr, &val1);
    ARRAY_PUSH(arr, &val2);
    ARRAY_PUSH(arr, &val3);
    
    ASSERT_EQ(ARRAY_SIZE(arr), 3);
    ASSERT_EQ(arr[0], 1);
    ASSERT_EQ(arr[1], 2);
    ASSERT_EQ(arr[2], 3);
    
    int popped;
    ARRAY_POP(arr, &popped);
    ASSERT_EQ(popped, 3);
    ASSERT_EQ(ARRAY_SIZE(arr), 2);
    
    ARRAY_FREE(arr);
}

TEST(array_clear) {
    int* arr = ARRAY_CREATE(int);
    int val = 42;
    
    for (int i = 0; i < 5; i++) {
        ARRAY_PUSH(arr, &val);
    }
    
    ASSERT_EQ(ARRAY_SIZE(arr), 5);
    
    ARRAY_CLEAR(arr);
    ASSERT_EQ(ARRAY_SIZE(arr), 0);
    
    ARRAY_FREE(arr);
}

// ============================================================================
// String Tests
// ============================================================================

TEST(string_dup) {
    char* dup = str_dup("hello");
    ASSERT(dup != NULL, "str_dup failed");
    ASSERT_EQ(strcmp(dup, "hello"), 0);
    FREE(dup);
}

TEST(string_concat) {
    char* concat = str_concat("hello", " world");
    ASSERT(concat != NULL, "str_concat failed");
    ASSERT_EQ(strcmp(concat, "hello world"), 0);
    FREE(concat);
}

TEST(string_starts_with) {
    ASSERT(str_starts_with("hello world", "hello"), "Should start with");
    ASSERT(!str_starts_with("hello world", "world"), "Should not start with");
}

TEST(string_ends_with) {
    ASSERT(str_ends_with("hello world", "world"), "Should end with");
    ASSERT(!str_ends_with("hello world", "hello"), "Should not end with");
}

TEST(string_trim) {
    char str1[] = "  hello  ";
    char* trimmed = str_trim(str1);
    ASSERT_EQ(strcmp(trimmed, "hello"), 0);
    
    char str2[] = "\t\nhello\r\n";
    trimmed = str_trim(str2);
    ASSERT_EQ(strcmp(trimmed, "hello"), 0);
}

TEST(string_case_insensitive) {
    ASSERT_EQ(strcasecmp_custom("Hello", "hello"), 0);
    ASSERT_EQ(strcasecmp_custom("WORLD", "world"), 0);
    ASSERT(strcasecmp_custom("abc", "abd") < 0, "Should be less than");
}

// ============================================================================
// Time Tests
// ============================================================================

TEST(time_now) {
    uint64_t t1 = time_now_ms();
    uint64_t t2 = time_now_us();
    
    ASSERT(t1 > 0, "time_now_ms returned 0");
    ASSERT(t2 > 0, "time_now_us returned 0");
    ASSERT(t2 >= t1 * 1000, "Microseconds should be >= milliseconds * 1000");
}

TEST(time_delta) {
    uint64_t start = time_now_ms();
    
    // Sleep for a bit
    struct timespec ts = {0, 10000000};  // 10ms
    nanosleep(&ts, NULL);
    
    uint64_t delta = time_delta_ms(start);
    ASSERT(delta >= 5, "Delta should be at least 5ms");
}

TEST(timer) {
    Timer timer = timer_start();
    
    struct timespec ts = {0, 50000000};  // 50ms
    nanosleep(&ts, NULL);
    
    uint64_t elapsed = timer_elapsed_ms(&timer);
    ASSERT(elapsed >= 40, "Timer should measure ~50ms");
    
    timer_stop(&timer);
    uint64_t final = timer_elapsed_ms(&timer);
    ASSERT(final >= 40, "Stopped timer should keep time");
}

// ============================================================================
// Math Tests
// ============================================================================

TEST(clamp) {
    ASSERT_EQ(clamp_int(5, 0, 10), 5);
    ASSERT_EQ(clamp_int(-5, 0, 10), 0);
    ASSERT_EQ(clamp_int(15, 0, 10), 10);
    
    ASSERT_FLOAT_EQ(clamp_float(0.5f, 0.0f, 1.0f), 0.5f, 0.001f);
    ASSERT_FLOAT_EQ(clamp_float(-0.5f, 0.0f, 1.0f), 0.0f, 0.001f);
}

TEST(lerp) {
    ASSERT_FLOAT_EQ(lerp(0.0f, 10.0f, 0.0f), 0.0f, 0.001f);
    ASSERT_FLOAT_EQ(lerp(0.0f, 10.0f, 1.0f), 10.0f, 0.001f);
    ASSERT_FLOAT_EQ(lerp(0.0f, 10.0f, 0.5f), 5.0f, 0.001f);
}

TEST(smoothstep) {
    ASSERT_FLOAT_EQ(smoothstep(0.0f, 1.0f, 0.0f), 0.0f, 0.001f);
    ASSERT_FLOAT_EQ(smoothstep(0.0f, 1.0f, 1.0f), 1.0f, 0.001f);
    ASSERT_FLOAT_EQ(smoothstep(0.0f, 1.0f, 0.5f), 0.5f, 0.01f);
}

TEST(map_range) {
    ASSERT_FLOAT_EQ(map_range(5.0f, 0.0f, 10.0f, 0.0f, 100.0f), 50.0f, 0.001f);
}

TEST(deg_rad) {
    ASSERT_FLOAT_EQ(deg_to_rad(180.0f), M_PI, 0.001f);
    ASSERT_FLOAT_EQ(rad_to_deg(M_PI), 180.0f, 0.001f);
    ASSERT_FLOAT_EQ(deg_to_rad(90.0f), M_PI_2, 0.001f);
}

// ============================================================================
// Rectangle Tests
// ============================================================================

TEST(rect_contains) {
    Rect r = {0, 0, 100, 100};
    
    ASSERT(rect_contains(&r, 50, 50), "Should contain center");
    ASSERT(!rect_contains(&r, 150, 50), "Should not contain outside");
    ASSERT(!rect_contains(&r, -10, 50), "Should not contain negative");
}

TEST(rect_intersects) {
    Rect a = {0, 0, 100, 100};
    Rect b = {50, 50, 100, 100};
    Rect c = {200, 200, 100, 100};
    
    ASSERT(rect_intersects(&a, &b), "Should intersect");
    ASSERT(!rect_intersects(&a, &c), "Should not intersect");
}

TEST(rect_intersection) {
    Rect a = {0, 0, 100, 100};
    Rect b = {50, 50, 100, 100};
    
    Rect result = rect_intersection(&a, &b);
    
    ASSERT_EQ(result.x, 50);
    ASSERT_EQ(result.y, 50);
    ASSERT_EQ(result.width, 50);
    ASSERT_EQ(result.height, 50);
}

TEST(rect_union) {
    Rect a = {0, 0, 100, 100};
    Rect b = {50, 50, 100, 100};
    
    Rect result = rect_union(&a, &b);
    
    ASSERT_EQ(result.x, 0);
    ASSERT_EQ(result.y, 0);
    ASSERT_EQ(result.width, 150);
    ASSERT_EQ(result.height, 150);
}

TEST(rect_equals) {
    Rect a = {0, 0, 100, 100};
    Rect b = {0, 0, 100, 100};
    Rect c = {0, 0, 50, 50};
    
    ASSERT(rect_equals(&a, &b), "Should be equal");
    ASSERT(!rect_equals(&a, &c), "Should not be equal");
}

// ============================================================================
// Bit Tests
// ============================================================================

TEST(bit_operations) {
    uint32_t val = 0;
    
    val = bit_set(val, 3);
    ASSERT(bit_is_set(val, 3), "Bit 3 should be set");
    ASSERT(!bit_is_set(val, 2), "Bit 2 should not be set");
    
    val = bit_toggle(val, 3);
    ASSERT(!bit_is_set(val, 3), "Bit 3 should be toggled off");
    
    val = bit_set(val, 3);
    val = bit_clear(val, 3);
    ASSERT(!bit_is_set(val, 3), "Bit 3 should be cleared");
}

TEST(bit_count) {
    ASSERT_EQ(bit_count(0), 0);
    ASSERT_EQ(bit_count(1), 1);
    ASSERT_EQ(bit_count(7), 3);  // 0b111
    ASSERT_EQ(bit_count(255), 8);  // 0b11111111
}

TEST(power_of_2) {
    ASSERT(is_power_of_2(1), "1 is power of 2");
    ASSERT(is_power_of_2(2), "2 is power of 2");
    ASSERT(is_power_of_2(4), "4 is power of 2");
    ASSERT(is_power_of_2(1024), "1024 is power of 2");
    ASSERT(!is_power_of_2(3), "3 is not power of 2");
    ASSERT(!is_power_of_2(100), "100 is not power of 2");
    
    ASSERT_EQ(next_power_of_2(1), 1);
    ASSERT_EQ(next_power_of_2(5), 8);
    ASSERT_EQ(next_power_of_2(100), 128);
}

// ============================================================================
// Color Tests
// ============================================================================

TEST(color_from_rgba) {
    Color c = color_from_rgba(1.0f, 0.5f, 0.25f, 0.75f);
    
    ASSERT_FLOAT_EQ(c.r, 1.0f, 0.001f);
    ASSERT_FLOAT_EQ(c.g, 0.5f, 0.001f);
    ASSERT_FLOAT_EQ(c.b, 0.25f, 0.001f);
    ASSERT_FLOAT_EQ(c.a, 0.75f, 0.001f);
}

TEST(color_from_hex) {
    Color c1 = color_from_hex("#FF0000");
    ASSERT_FLOAT_EQ(c1.r, 1.0f, 0.01f);
    ASSERT_FLOAT_EQ(c1.g, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(c1.b, 0.0f, 0.01f);
    
    Color c2 = color_from_hex("#00FF0080");
    ASSERT_FLOAT_EQ(c2.r, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(c2.g, 1.0f, 0.01f);
    ASSERT_FLOAT_EQ(c2.b, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(c2.a, 0.5f, 0.01f);
}

TEST(color_to_rgba32) {
    Color c = color_from_rgba(1.0f, 0.5f, 0.25f, 1.0f);
    uint32_t rgba = color_to_rgba32(&c);
    
    ASSERT_EQ((rgba >> 24) & 0xFF, 255);  // A
    ASSERT_EQ((rgba >> 16) & 0xFF, 255);  // R
    // G and B have rounding due to float->int conversion
    ASSERT(((rgba >> 8) & 0xFF) >= 127 && ((rgba >> 8) & 0xFF) <= 128, "G channel");
    ASSERT((rgba & 0xFF) >= 63 && (rgba & 0xFF) <= 64, "B channel");
}

TEST(color_lerp) {
    Color a = color_from_rgba(0.0f, 0.0f, 0.0f, 1.0f);
    Color b = color_from_rgba(1.0f, 1.0f, 1.0f, 1.0f);
    
    Color result = color_lerp(&a, &b, 0.5f);
    
    ASSERT_FLOAT_EQ(result.r, 0.5f, 0.001f);
    ASSERT_FLOAT_EQ(result.g, 0.5f, 0.001f);
    ASSERT_FLOAT_EQ(result.b, 0.5f, 0.001f);
}

// ============================================================================
// Hash Tests
// ============================================================================

TEST(hash_string) {
    uint32_t h1 = hash_string("hello");
    uint32_t h2 = hash_string("hello");
    uint32_t h3 = hash_string("world");
    
    ASSERT(h1 == h2, "Same string should have same hash");
    ASSERT(h1 != h3, "Different strings should have different hashes");
}

TEST(hash_int) {
    uint32_t h1 = hash_int(42);
    uint32_t h2 = hash_int(42);
    uint32_t h3 = hash_int(43);
    
    ASSERT(h1 == h2, "Same int should have same hash");
    ASSERT(h1 != h3, "Different ints should have different hashes");
}

// ============================================================================
// Min/Max Tests
// ============================================================================

TEST(min_max_int) {
    ASSERT_EQ(min_int(5, 10), 5);
    ASSERT_EQ(min_int(10, 5), 5);
    ASSERT_EQ(max_int(5, 10), 10);
    ASSERT_EQ(max_int(10, 5), 10);
}

TEST(min_max_float) {
    ASSERT_FLOAT_EQ(min_float(5.5f, 10.5f), 5.5f, 0.001f);
    ASSERT_FLOAT_EQ(max_float(5.5f, 10.5f), 10.5f, 0.001f);
}

// ============================================================================
// Macro Tests
// ============================================================================

TEST(macros_swap) {
    int a = 5, b = 10;
    SWAP(a, b);
    ASSERT_EQ(a, 10);
    ASSERT_EQ(b, 5);
}

TEST(macros_min_max) {
    ASSERT_EQ(MIN(5, 10), 5);
    ASSERT_EQ(MAX(5, 10), 10);
    ASSERT_EQ(CLAMP(5, 0, 10), 5);
    ASSERT_EQ(CLAMP(-5, 0, 10), 0);
    ASSERT_EQ(CLAMP(15, 0, 10), 10);
}

TEST(macros_abs_sign) {
    ASSERT_EQ(ABS(-5), 5);
    ASSERT_EQ(ABS(5), 5);
    ASSERT_EQ(SIGN(-5), -1);
    ASSERT_EQ(SIGN(5), 1);
    ASSERT_EQ(SIGN(0), 0);
}

TEST(array_length) {
    int arr[] = {1, 2, 3, 4, 5};
    ASSERT_EQ(ARRAY_LENGTH(arr), 5);
}

// ============================================================================
// Callback Tests
// ============================================================================

static int callback_value = 0;
static void* callback_user_data = NULL;

static void test_callback(void* user_data) {
    callback_value = 1;
    callback_user_data = user_data;
}

TEST(callback_invoke) {
    callback_value = 0;
    callback_user_data = NULL;
    
    int user_data_val = 42;
    callback_invoke(test_callback, &user_data_val);
    
    ASSERT_EQ(callback_value, 1);
    ASSERT(callback_user_data == &user_data_val, "User data should match");
}

// ============================================================================
// List Tests
// ============================================================================

typedef struct {
    ListNode node;
    int value;
} TestListNode;

TEST(list_operations) {
    TestListNode head, node1, node2;
    
    list_node_init(&head.node);
    list_node_init(&node1.node);
    list_node_init(&node2.node);
    
    node1.value = 1;
    node2.value = 2;
    
    ASSERT(list_is_empty(&head.node), "List should be empty");
    
    list_add_after(&head.node, &node1.node);
    ASSERT(!list_is_empty(&head.node), "List should not be empty");
    
    list_add_after(&node1.node, &node2.node);
    
    ASSERT(list_next(&head.node) == &node1.node, "Next should be node1");
    ASSERT(list_next(&node1.node) == &node2.node, "Next should be node2");
    ASSERT(list_prev(&node2.node) == &node1.node, "Prev should be node1");
    
    list_remove(&node1.node);
    ASSERT(list_next(&head.node) == &node2.node, "After remove, next should be node2");
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(void) {
    printf("=== Common.h Utilities Test Suite ===\n\n");
    
    // Memory tests
    RUN_TEST(memory_alloc);
    RUN_TEST(memory_malloc);
    RUN_TEST(memory_realloc);
    
    // Array tests
    RUN_TEST(array_basic);
    RUN_TEST(array_clear);
    
    // String tests
    RUN_TEST(string_dup);
    RUN_TEST(string_concat);
    RUN_TEST(string_starts_with);
    RUN_TEST(string_ends_with);
    RUN_TEST(string_trim);
    RUN_TEST(string_case_insensitive);
    
    // Time tests
    RUN_TEST(time_now);
    RUN_TEST(time_delta);
    RUN_TEST(timer);
    
    // Math tests
    RUN_TEST(clamp);
    RUN_TEST(lerp);
    RUN_TEST(smoothstep);
    RUN_TEST(map_range);
    RUN_TEST(deg_rad);
    
    // Rectangle tests
    RUN_TEST(rect_contains);
    RUN_TEST(rect_intersects);
    RUN_TEST(rect_intersection);
    RUN_TEST(rect_union);
    RUN_TEST(rect_equals);
    
    // Bit tests
    RUN_TEST(bit_operations);
    RUN_TEST(bit_count);
    RUN_TEST(power_of_2);
    
    // Color tests
    RUN_TEST(color_from_rgba);
    RUN_TEST(color_from_hex);
    RUN_TEST(color_to_rgba32);
    RUN_TEST(color_lerp);
    
    // Hash tests
    RUN_TEST(hash_string);
    RUN_TEST(hash_int);
    
    // Min/Max tests
    RUN_TEST(min_max_int);
    RUN_TEST(min_max_float);
    
    // Macro tests
    RUN_TEST(macros_swap);
    RUN_TEST(macros_min_max);
    RUN_TEST(macros_abs_sign);
    RUN_TEST(array_length);
    
    // Callback tests
    RUN_TEST(callback_invoke);
    
    // List tests
    RUN_TEST(list_operations);
    
    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_run);
    
    if (tests_failed > 0) {
        printf("\nSome tests failed!\n");
        return 1;
    }
    
    printf("\nAll tests passed!\n");
    return 0;
}
