#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_FATAL
} LogLevel;

// Debug categories (bitmask)
typedef enum {
    LOG_CAT_NONE      = 0,
    LOG_CAT_INPUT     = (1 << 0),
    LOG_CAT_RENDER    = (1 << 1),
    LOG_CAT_PLUGIN    = (1 << 2),
    LOG_CAT_SCENE     = (1 << 3),
    LOG_CAT_IPC       = (1 << 4),
    LOG_CAT_ALL       = 0xFFFF
} LogCategory;

typedef struct {
    FILE* file;
    LogLevel level;
    int initialized;
    LogCategory categories;  // Enabled debug categories
} Logger;

// Get singleton logger instance
Logger* logger_get_instance(void);

// Initialize logger (call once at startup)
void logger_init(LogLevel level);

// Set log level
void logger_set_level(LogLevel level);

// Core logging function with printf-style formatting
void logger_log(LogLevel level, const char* format, ...);

// Set debug category mask (0 = none, LOG_CAT_ALL = all)
void logger_set_categories(LogCategory categories);

// Enable specific debug category
void logger_enable_category(LogCategory category);

// Disable specific debug category
void logger_disable_category(LogCategory category);

// Check if category is enabled
bool logger_category_enabled(LogCategory category);

// Category-specific logging macros
#define LOG_DEBUG_CAT(cat, ...) do { \
    if (logger_category_enabled(cat)) logger_log(LOG_DEBUG, __VA_ARGS__); \
} while(0)

#define LOG_INPUT(...)  LOG_DEBUG_CAT(LOG_CAT_INPUT, __VA_ARGS__)
#define LOG_RENDER(...) LOG_DEBUG_CAT(LOG_CAT_RENDER, __VA_ARGS__)
#define LOG_PLUGIN(...) LOG_DEBUG_CAT(LOG_CAT_PLUGIN, __VA_ARGS__)
#define LOG_SCENE(...)  LOG_DEBUG_CAT(LOG_CAT_SCENE, __VA_ARGS__)
#define LOG_IPC(...)    LOG_DEBUG_CAT(LOG_CAT_IPC, __VA_ARGS__)

// Convenience macros
#define LOG_DEBUG(...) logger_log(LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  logger_log(LOG_INFO, __VA_ARGS__)
#define LOG_WARN(...)  logger_log(LOG_WARNING, __VA_ARGS__)
#define LOG_ERROR(...) logger_log(LOG_ERROR, __VA_ARGS__)
#define LOG_FATAL(...) logger_log(LOG_FATAL, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
