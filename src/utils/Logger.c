#include "Logger.h"
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

static Logger g_logger = {
    .file = NULL,
    .level = LOG_INFO,
    .initialized = 0
};

static const char* level_strings[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
};

static const char* level_colors[] = {
    "\033[36m",  // Cyan - DEBUG
    "\033[32m",  // Green - INFO
    "\033[33m",  // Yellow - WARN
    "\033[31m",  // Red - ERROR
    "\033[35m"   // Magenta - FATAL
};

static const char* color_reset = "\033[0m";

static void ensure_log_directory(void) {
    const char* home = getenv("HOME");
    if (!home) return;
    
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.local/share/havel/logs", home);
    
    // Create parent directories
    mkdir(dir, 0755);
}

static FILE* open_log_file(void) {
    const char* home = getenv("HOME");
    if (!home) return stderr;
    
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/.local/share/havel/logs/havel-wm.log", home);
    
    FILE* f = fopen(filepath, "a");
    if (!f) {
        return stderr;
    }
    
    return f;
}

Logger* logger_get_instance(void) {
    return &g_logger;
}

void logger_init(LogLevel level) {
    if (g_logger.initialized) {
        return;
    }
    
    ensure_log_directory();
    
    g_logger.file = open_log_file();
    g_logger.level = level;
    g_logger.initialized = 1;
    
    // Log startup message
    logger_log(LOG_INFO, "=== Havel WM starting ===");
    logger_log(LOG_INFO, "Log file: ~/.local/share/havel/logs/havel-wm.log");
}

void logger_set_level(LogLevel level) {
    g_logger.level = level;
}

void logger_log(LogLevel level, const char* format, ...) {
    if (!g_logger.initialized || level < g_logger.level) {
        return;
    }
    
    // Get timestamp
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm* tm_info = localtime(&tv.tv_sec);
    
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Build message
    char message[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    // Write to file
    if (g_logger.file && g_logger.file != stderr) {
        fprintf(g_logger.file, "%s.%03ld [%s] %s\n", 
                timestamp, tv.tv_usec / 1000, level_strings[level], message);
        fflush(g_logger.file);
    }
    
    // Write to stderr with color
    fprintf(stderr, "%s%s.%03ld [%s] %s%s\n", 
            level_colors[level],
            timestamp, tv.tv_usec / 1000, level_strings[level], message, color_reset);
    fflush(stderr);
}
