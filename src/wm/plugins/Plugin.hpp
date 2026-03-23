#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>
#include <unordered_map>
#include <any>

namespace havel {

// Forward declarations
class CompositorAPI;
struct OutputFrameEvent;
struct ViewEvent;
struct KeyEvent;
class Plugin;

// ============================================================================
// Plugin Metadata
// ============================================================================

/**
 * Plugin information and capabilities
 */
struct PluginInfo {
    std::string name;           // Unique plugin identifier
    std::string version;        // Semantic version (major.minor.patch)
    std::string author;         // Author name
    std::string description;    // Short description
    uint32_t apiVersion;        // Required plugin API version
    std::vector<std::string> dependencies;  // Required plugins
    
    // Plugin capabilities
    bool providesOverlay = false;
    bool providesKeybindings = false;
    bool providesSettings = false;
    
    // Load priority (lower = loaded first)
    int priority = 100;
};

// ============================================================================
// Plugin Settings API (Type-safe)
// ============================================================================

/**
 * Type-safe plugin settings
 */
class PluginSettings {
public:
    // Getters with defaults
    bool getBool(const std::string& key, bool defaultValue = false) const;
    int getInt(const std::string& key, int defaultValue = 0) const;
    float getFloat(const std::string& key, float defaultValue = 0.0f) const;
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    
    // Setters
    void setBool(const std::string& key, bool value);
    void setInt(const std::string& key, int value);
    void setFloat(const std::string& key, float value);
    void setString(const std::string& key, const std::string& value);
    
    // Check if key exists
    bool hasKey(const std::string& key) const;
    
    // Get all keys
    std::vector<std::string> getKeys() const;
    
    // Load from JSON-like map
    void loadFromMap(const std::unordered_map<std::string, std::string>& data);
    
    // Export to map
    std::unordered_map<std::string, std::string> exportToMap() const;
    
private:
    std::unordered_map<std::string, std::any> m_values;
};

// ============================================================================
// Plugin Event System
// ============================================================================

/**
 * Event types for plugin communication
 */
enum class PluginEventType {
    // Window events
    WindowMapped,
    WindowUnmapped,
    WindowDestroyed,
    WindowFocused,
    WindowMoved,
    WindowResized,
    
    // Workspace events
    WorkspaceChanged,
    WorkspaceCreated,
    WorkspaceDestroyed,
    
    // Output events
    OutputAdded,
    OutputRemoved,
    OutputResolutionChanged,
    
    // Plugin events
    PluginLoaded,
    PluginUnloaded,
    SettingsChanged,
    
    // Custom events
    Custom
};

/**
 * Plugin event data
 */
struct PluginEvent {
    PluginEventType type;
    std::string source;       // Plugin that emitted the event
    std::any data;            // Event-specific data
    uint64_t timestamp;       // Event timestamp (ms since startup)
};

/**
 * Event listener callback
 */
using EventListener = std::function<void(const PluginEvent&)>;

// ============================================================================
// Plugin Logger
// ============================================================================

/**
 * Per-plugin logging with levels
 */
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class PluginLogger {
public:
    PluginLogger(const std::string& pluginName);
    
    void log(LogLevel level, const std::string& message);
    void debug(const std::string& message);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);
    
    // Set minimum log level for this plugin
    void setMinLevel(LogLevel level);
    
private:
    std::string m_pluginName;
    LogLevel m_minLevel = LogLevel::Info;
};

// ============================================================================
// Plugin Interface (Improved)
// ============================================================================

/**
 * Enhanced Plugin interface
 *
 * Plugins are modular components that extend compositor functionality.
 * They have a lifecycle, can communicate via events, and have type-safe settings.
 */
class Plugin {
public:
    virtual ~Plugin() = default;

    // -------------------------------------------------------------------------
    // Plugin Metadata
    // -------------------------------------------------------------------------
    
    /**
     * Get plugin information
     * Default implementation uses name() and version()
     * Override for full metadata (author, description, etc.)
     */
    virtual PluginInfo getInfo() const {
        PluginInfo info;
        info.name = name();
        info.version = version();
        info.apiVersion = 1;
        info.priority = 100;
        return info;
    }
    
    // Legacy methods (for backward compatibility)
    virtual const char* name() const = 0;
    virtual const char* version() const = 0;
    
    // Convenience methods
    std::string getName() const { return name(); }
    std::string getVersion() const { return version(); }
    
    // -------------------------------------------------------------------------
    // Lifecycle (required)
    // -------------------------------------------------------------------------
    
    /**
     * Initialize plugin
     * Called when plugin is loaded
     */
    virtual void init(CompositorAPI* api) = 0;
    
    /**
     * Shutdown plugin
     * Called when plugin is unloaded
     */
    virtual void fini() = 0;
    
    // Optional lifecycle hooks
    virtual void preInit() {}
    virtual void postInit() {}
    virtual void preShutdown() {}
    virtual void postShutdown() {}
    
    // -------------------------------------------------------------------------
    // Configuration (optional)
    // -------------------------------------------------------------------------
    
    /**
     * Get plugin settings
     */
    virtual PluginSettings& getSettings() { return m_settings; }
    virtual const PluginSettings& getSettings() const { return m_settings; }
    
    /**
     * Called when settings are loaded
     */
    virtual void onSettingsLoaded() {}
    
    /**
     * Called when a setting changes
     */
    virtual void onSettingChanged(const std::string& key) { (void)key; }
    
    /**
     * Load configuration (legacy - use onSettingsLoaded instead)
     */
    virtual void loadConfig(const std::string& configPath) { 
        (void)configPath; 
    }
    
    // -------------------------------------------------------------------------
    // Event Handling (optional)
    // -------------------------------------------------------------------------
    
    /**
     * Handle plugin events
     * Return true to consume event (prevent other plugins from receiving)
     */
    virtual bool onEvent(const PluginEvent& event) { (void)event; return false; }
    
    /**
     * Emit an event to other plugins
     */
    void emitEvent(PluginEvent& event);
    
    // -------------------------------------------------------------------------
    // Event Hooks (override as needed)
    // -------------------------------------------------------------------------
    
    virtual void onOutputFrame(const OutputFrameEvent& event) { (void)event; }
    virtual void onViewMap(const ViewEvent& event) { (void)event; }
    virtual void onViewUnmap(const ViewEvent& event) { (void)event; }
    virtual void onViewDestroy(const ViewEvent& event) { (void)event; }
    virtual bool onKey(const KeyEvent& event) { (void)event; return false; }
    
    // -------------------------------------------------------------------------
    // Overlay Rendering (optional)
    // -------------------------------------------------------------------------
    
    /**
     * Render overlay content
     * Called during render pass if plugin provides overlay
     */
    virtual void renderOverlay(void* renderer) { (void)renderer; }
    
    // -------------------------------------------------------------------------
    // Input Handling (optional)
    // -------------------------------------------------------------------------
    
    virtual void onMouseMotion(int x, int y) { (void)x; (void)y; }
    virtual void onMouseButton(uint32_t button, bool pressed, int x, int y) {
        (void)button; (void)pressed; (void)x; (void)y;
    }
    
    // -------------------------------------------------------------------------
    // Logging (protected)
    // -------------------------------------------------------------------------
    
protected:
    Plugin() = default;
    
    PluginLogger& logger() { return m_logger; }
    const PluginLogger& logger() const { return m_logger; }
    
private:
    PluginSettings m_settings;
    mutable PluginLogger m_logger{"Plugin"};  // Default name, changed in init()
    
    friend class PluginManager;  // For event emission
};

// ============================================================================
// Event Data Structures
// ============================================================================

struct OutputFrameEvent {
    void* output;        // wlr_output*
    void* sceneOutput;   // wlr_scene_output*
    int width;
    int height;
    float refresh;       // mHz
};

struct ViewEvent {
    void* view;          // View*
    const char* appId;
    const char* title;
    uint32_t workspace;
    int x, y, width, height;
};

struct KeyEvent {
    uint32_t keycode;
    uint32_t modifiers;
    bool pressed;
    uint32_t keysym;    // XKB keysym (layout-aware)
    char key_char;      // ASCII character from keysym
    char utf8[8];       // UTF-8 encoded character
};

// ============================================================================
// Plugin Factory (for Level 3 - runtime loading)
// ============================================================================

/**
 * Plugin factory function type
 * For Level 2 (compiled-in): plugins are created directly
 * For Level 3 (runtime): plugins export these functions
 */
using PluginCreateFn = Plugin* (*)();
using PluginDestroyFn = void (*)(Plugin*);

/**
 * Plugin factory registration (for Level 2)
 */
struct PluginFactory {
    std::string name;
    std::function<std::unique_ptr<Plugin>()> create;
};

// Macro for registering Level 2 plugins
#define REGISTER_PLUGIN(PluginClass) \
    static PluginFactory g_##PluginClass##_factory = { \
        #PluginClass, \
        []() { return std::make_unique<PluginClass>(); } \
    }

} // namespace havel
