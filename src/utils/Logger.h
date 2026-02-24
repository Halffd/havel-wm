#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

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

typedef struct {
    FILE* file;
    LogLevel level;
    int initialized;
} Logger;

// Get singleton logger instance
Logger* logger_get_instance(void);

// Initialize logger (call once at startup)
void logger_init(LogLevel level);

// Set log level
void logger_set_level(LogLevel level);

// Core logging function with printf-style formatting
void logger_log(LogLevel level, const char* format, ...);

// Convenience macros
#define LOG_DEBUG(...) logger_log(LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  logger_log(LOG_INFO, __VA_ARGS__)
#define LOG_WARN(...)  logger_log(LOG_WARNING, __VA_ARGS__)
#define LOG_ERROR(...) logger_log(LOG_ERROR, __VA_ARGS__)
#define LOG_FATAL(...) logger_log(LOG_FATAL, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
