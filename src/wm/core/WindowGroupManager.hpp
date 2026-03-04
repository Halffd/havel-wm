// Window Group Manager - Tabbed windows, groups, layouts

#pragma once

#include <wm/Types.hpp>
#include <vector>
#include <memory>
#include <string>
#include <functional>

namespace havel {

class View;

/**
 * Window group types
 */
enum class GroupType {
    None = 0,
    Tabbed,         // Tabbed windows (one visible at a time)
    Stacked,        // Stacked windows (title bars visible)
    SplitH,         // Horizontal split
    SplitV,         // Vertical split
    Floating,       // Floating group
    Grid,           // Grid layout
};

/**
 * Window group representation
 */
struct WindowGroup {
    uint64_t id = 0;
    std::string name;
    GroupType type = GroupType::None;
    std::vector<View*> windows;
    int activeIndex = 0;  // For tabbed/stacked
    Rect geometry;
    bool collapsed = false;
    uint32_t workspace = 0;
    
    // Layout-specific settings
    float splitRatio = 0.5f;  // For split layouts
    int gridCols = 2;         // For grid layout
    int gridRows = 2;
};

/**
 * Window rules for automatic grouping
 */
struct WindowRule {
    std::string appId;
    std::string title;
    std::string workspace;
    GroupType groupType = GroupType::None;
    bool floating = false;
    bool fullscreen = false;
    bool alwaysOnTop = false;
    bool sticky = false;  // All workspaces
    Rect geometry;
    bool opacitySet = false;
    float opacity = 1.0f;
};

/**
 * Window Group Manager - Complete window grouping
 * 
 * Features:
 * - Window grouping (tabbed, stacked, split)
 * - Window rules (auto-grouping, positioning)
 * - Window decorations
 * - Window focus management
 * - Window operations (move, resize, minimize, maximize)
 */
class WindowGroupManager {
public:
    WindowGroupManager();
    ~WindowGroupManager();

    // Initialize
    bool initialize();
    void shutdown();

    // Window grouping
    WindowGroup* createGroup(GroupType type, const std::string& name = "");
    void destroyGroup(uint64_t groupId);
    
    // Add/remove windows from groups
    bool addWindowToGroup(View* view, uint64_t groupId);
    bool removeWindowFromGroup(View* view);
    bool removeWindowFromGroup(uint64_t groupId, int windowIndex);
    
    // Group operations
    void switchGroupWindow(uint64_t groupId, int index);
    void nextGroupWindow(uint64_t groupId);
    void prevGroupWindow(uint64_t groupId);
    void splitGroup(uint64_t groupId, GroupType newType);
    void mergeGroups(uint64_t groupId1, uint64_t groupId2);
    
    // Group navigation
    WindowGroup* getGroup(uint64_t groupId);
    WindowGroup* getWindowGroup(View* view);
    const std::vector<std::unique_ptr<WindowGroup>>& getAllGroups() const { return m_groups; }
    
    // Window rules
    void addWindowRule(const WindowRule& rule);
    void removeWindowRule(const std::string& appId);
    void clearWindowRules();
    const std::vector<WindowRule>& getWindowRules() const { return m_rules; }
    
    // Apply rules to window
    void applyWindowRules(View* view);
    
    // Window operations
    void minimizeWindow(View* view);
    void maximizeWindow(View* view);
    void fullscreenWindow(View* view, bool fullscreen);
    void toggleAlwaysOnTop(View* view);
    void toggleSticky(View* view);  // All workspaces
    void closeWindow(View* view);
    
    // Window state queries
    bool isMinimized(View* view) const;
    bool isMaximized(View* view) const;
    bool isFullscreen(View* view) const;
    bool isAlwaysOnTop(View* view) const;
    bool isSticky(View* view) const;
    
    // Focus management
    void focusWindow(View* view);
    View* getFocusedWindow() const { return m_focusedWindow; }
    
    // Window list
    std::vector<View*> getAllWindows() const;
    std::vector<View*> getVisibleWindows() const;
    std::vector<View*> getWindowsOnWorkspace(uint32_t workspace) const;
    
    // Callbacks
    using WindowCallback = std::function<void(View*)>;
    using GroupCallback = std::function<void(WindowGroup*)>;
    
    void setOnWindowFocus(WindowCallback cb) { m_onWindowFocus = cb; }
    void setOnWindowClose(WindowCallback cb) { m_onWindowClose = cb; }
    void setOnGroupChange(GroupCallback cb) { m_onGroupChange = cb; }

private:
    // Group management
    uint64_t generateGroupId();
    void updateGroupGeometry(uint64_t groupId);
    void arrangeGroupWindows(uint64_t groupId);
    
    // Window state tracking
    struct WindowState {
        bool minimized = false;
        bool maximized = false;
        bool fullscreen = false;
        bool alwaysOnTop = false;
        bool sticky = false;
        Rect restoreGeometry;  // For restore after maximize
    };
    
    std::unordered_map<View*, WindowState> m_windowStates;
    std::vector<std::unique_ptr<WindowGroup>> m_groups;
    std::vector<WindowRule> m_rules;
    
    View* m_focusedWindow = nullptr;
    uint64_t m_nextGroupId = 1;
    
    WindowCallback m_onWindowFocus;
    WindowCallback m_onWindowClose;
    GroupCallback m_onGroupChange;
};

/**
 * Global window manager access
 */
WindowGroupManager& getWindowGroupManager();

} // namespace havel
