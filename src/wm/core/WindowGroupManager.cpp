// Window Group Manager Implementation

#include "WindowGroupManager.hpp"
#include <wm/View.hpp>
#include <Logger.h>
#include <algorithm>

namespace havel {

// Global window manager instance
static WindowGroupManager* g_windowManager = nullptr;

WindowGroupManager& getWindowGroupManager() {
    if (!g_windowManager) {
        g_windowManager = new WindowGroupManager();
        g_windowManager->initialize();
    }
    return *g_windowManager;
}

// ============================================================================
// WindowGroupManager Implementation
// ============================================================================

WindowGroupManager::WindowGroupManager() = default;

WindowGroupManager::~WindowGroupManager() {
    shutdown();
}

bool WindowGroupManager::initialize() {
    LOG_INFO("[WindowGroupManager] Initialized");
    return true;
}

void WindowGroupManager::shutdown() {
    m_groups.clear();
    m_rules.clear();
    m_windowStates.clear();
    m_focusedWindow = nullptr;
    LOG_INFO("[WindowGroupManager] Shutdown complete");
}

uint64_t WindowGroupManager::generateGroupId() {
    return m_nextGroupId++;
}

WindowGroup* WindowGroupManager::createGroup(GroupType type, const std::string& name) {
    auto group = std::make_unique<WindowGroup>();
    group->id = generateGroupId();
    group->type = type;
    group->name = name.empty() ? "Group " + std::to_string(group->id) : name;
    group->activeIndex = 0;
    group->splitRatio = 0.5f;
    group->gridCols = 2;
    group->gridRows = 2;
    
    WindowGroup* ptr = group.get();
    m_groups.push_back(std::move(group));
    
    LOG_INFO("[WindowGroupManager] Created group %lu: %s (type=%d)", 
             ptr->id, ptr->name.c_str(), static_cast<int>(type));
    
    if (m_onGroupChange) {
        m_onGroupChange(ptr);
    }
    
    return ptr;
}

void WindowGroupManager::destroyGroup(uint64_t groupId) {
    auto it = std::find_if(m_groups.begin(), m_groups.end(),
        [groupId](const std::unique_ptr<WindowGroup>& g) {
            return g->id == groupId;
        });
    
    if (it != m_groups.end()) {
        // Ungroup all windows first
        for (View* view : (*it)->windows) {
            if (view) {
                // Restore window visibility if tabbed
                if ((*it)->type == GroupType::Tabbed) {
                    // Would show window - handled by compositor
                }
            }
        }
        
        LOG_INFO("[WindowGroupManager] Destroyed group %lu", groupId);
        m_groups.erase(it);
        
        if (m_onGroupChange) {
            m_onGroupChange(nullptr);
        }
    }
}

bool WindowGroupManager::addWindowToGroup(View* view, uint64_t groupId) {
    if (!view) return false;
    
    WindowGroup* group = getGroup(groupId);
    if (!group) return false;
    
    // Remove from existing group if any
    removeWindowFromGroup(view);
    
    group->windows.push_back(view);
    
    // For tabbed groups, hide all but active window
    if (group->type == GroupType::Tabbed && group->activeIndex > 0) {
        // Would hide window - handled by compositor
    }
    
    LOG_INFO("[WindowGroupManager] Added window to group %lu (now %zu windows)",
             groupId, group->windows.size());
    
    arrangeGroupWindows(groupId);
    
    if (m_onGroupChange) {
        m_onGroupChange(group);
    }
    
    return true;
}

bool WindowGroupManager::removeWindowFromGroup(View* view) {
    if (!view) return false;
    
    for (auto& group : m_groups) {
        auto it = std::find(group->windows.begin(), group->windows.end(), view);
        if (it != group->windows.end()) {
            group->windows.erase(it);
            
            // Adjust active index if needed
            if (static_cast<size_t>(group->activeIndex) >= group->windows.size()) {
                group->activeIndex = std::max(0, static_cast<int>(group->windows.size()) - 1);
            }
            
            LOG_INFO("[WindowGroupManager] Removed window from group %lu", group->id);
            
            // Destroy empty groups
            if (group->windows.empty()) {
                destroyGroup(group->id);
            } else {
                arrangeGroupWindows(group->id);
            }
            
            if (m_onGroupChange) {
                m_onGroupChange(group.get());
            }
            
            return true;
        }
    }
    
    return false;
}

bool WindowGroupManager::removeWindowFromGroup(uint64_t groupId, int windowIndex) {
    WindowGroup* group = getGroup(groupId);
    if (!group || windowIndex < 0 || windowIndex >= static_cast<int>(group->windows.size())) {
        return false;
    }
    
    View* view = group->windows[windowIndex];
    group->windows.erase(group->windows.begin() + windowIndex);
    
    // Adjust active index
    if (group->activeIndex >= static_cast<int>(group->windows.size())) {
        group->activeIndex = std::max(0, static_cast<int>(group->windows.size()) - 1);
    }
    
    LOG_INFO("[WindowGroupManager] Removed window %d from group %lu", windowIndex, groupId);
    
    if (group->windows.empty()) {
        destroyGroup(groupId);
    } else {
        arrangeGroupWindows(groupId);
    }
    
    if (m_onGroupChange) {
        m_onGroupChange(group);
    }
    
    return true;
}

void WindowGroupManager::switchGroupWindow(uint64_t groupId, int index) {
    WindowGroup* group = getGroup(groupId);
    if (!group || index < 0 || index >= static_cast<int>(group->windows.size())) {
        return;
    }
    
    int oldIndex = group->activeIndex;
    group->activeIndex = index;
    
    // For tabbed groups: hide old, show new
    if (group->type == GroupType::Tabbed) {
        if (oldIndex >= 0 && oldIndex < static_cast<int>(group->windows.size())) {
            // Would hide old window
        }
        // Would show new window
        focusWindow(group->windows[index]);
    }
    
    LOG_INFO("[WindowGroupManager] Switched group %lu to window %d", groupId, index);
    
    if (m_onGroupChange) {
        m_onGroupChange(group);
    }
}

void WindowGroupManager::nextGroupWindow(uint64_t groupId) {
    WindowGroup* group = getGroup(groupId);
    if (!group || group->windows.empty()) return;
    
    int nextIndex = (group->activeIndex + 1) % group->windows.size();
    switchGroupWindow(groupId, nextIndex);
}

void WindowGroupManager::prevGroupWindow(uint64_t groupId) {
    WindowGroup* group = getGroup(groupId);
    if (!group || group->windows.empty()) return;
    
    int prevIndex = (group->activeIndex - 1 + group->windows.size()) % group->windows.size();
    switchGroupWindow(groupId, prevIndex);
}

void WindowGroupManager::splitGroup(uint64_t groupId, GroupType newType) {
    WindowGroup* group = getGroup(groupId);
    if (!group) return;
    
    group->type = newType;
    arrangeGroupWindows(groupId);
    
    LOG_INFO("[WindowGroupManager] Split group %lu to type %d", groupId, static_cast<int>(newType));
    
    if (m_onGroupChange) {
        m_onGroupChange(group);
    }
}

void WindowGroupManager::mergeGroups(uint64_t groupId1, uint64_t groupId2) {
    WindowGroup* group1 = getGroup(groupId1);
    WindowGroup* group2 = getGroup(groupId2);
    
    if (!group1 || !group2) return;
    
    // Move all windows from group2 to group1
    for (View* view : group2->windows) {
        group1->windows.push_back(view);
    }
    
    // Destroy group2
    destroyGroup(groupId2);
    
    arrangeGroupWindows(groupId1);
    
    LOG_INFO("[WindowGroupManager] Merged group %lu into %lu", groupId2, groupId1);
}

WindowGroup* WindowGroupManager::getGroup(uint64_t groupId) {
    for (auto& group : m_groups) {
        if (group->id == groupId) {
            return group.get();
        }
    }
    return nullptr;
}

WindowGroup* WindowGroupManager::getWindowGroup(View* view) {
    if (!view) return nullptr;
    
    for (auto& group : m_groups) {
        auto it = std::find(group->windows.begin(), group->windows.end(), view);
        if (it != group->windows.end()) {
            return group.get();
        }
    }
    
    return nullptr;
}

void WindowGroupManager::addWindowRule(const WindowRule& rule) {
    m_rules.push_back(rule);
    LOG_INFO("[WindowGroupManager] Added window rule for: %s", rule.appId.c_str());
}

void WindowGroupManager::removeWindowRule(const std::string& appId) {
    m_rules.erase(
        std::remove_if(m_rules.begin(), m_rules.end(),
            [&appId](const WindowRule& r) {
                return r.appId == appId;
            }),
        m_rules.end());
    
    LOG_INFO("[WindowGroupManager] Removed window rule for: %s", appId.c_str());
}

void WindowGroupManager::clearWindowRules() {
    m_rules.clear();
    LOG_INFO("[WindowGroupManager] Cleared all window rules");
}

void WindowGroupManager::applyWindowRules(View* view) {
    if (!view) return;
    
    const std::string& appId = view->appId();
    const std::string& title = view->title();
    
    for (const auto& rule : m_rules) {
        bool match = false;
        
        // Match by app ID
        if (!rule.appId.empty() && appId.find(rule.appId) != std::string::npos) {
            match = true;
        }
        
        // Match by title
        if (!rule.title.empty() && title.find(rule.title) != std::string::npos) {
            match = true;
        }
        
        if (match) {
            LOG_INFO("[WindowGroupManager] Applied rule to: %s - %s", appId.c_str(), title.c_str());
            
            // Apply geometry
            if (rule.geometry.w > 0 && rule.geometry.h > 0) {
                // Would set window geometry
            }
            
            // Apply floating
            if (rule.floating) {
                // Would set floating mode
            }
            
            // Apply fullscreen
            if (rule.fullscreen) {
                fullscreenWindow(view, true);
            }
            
            // Apply always on top
            if (rule.alwaysOnTop) {
                toggleAlwaysOnTop(view);
            }
            
            // Apply sticky
            if (rule.sticky) {
                toggleSticky(view);
            }
            
            // Apply opacity
            if (rule.opacitySet) {
                // Would set opacity
            }
            
            // Apply group type
            if (rule.groupType != GroupType::None) {
                // Would create or add to group
            }
        }
    }
}

void WindowGroupManager::minimizeWindow(View* view) {
    if (!view) return;
    
    m_windowStates[view].minimized = true;
    LOG_INFO("[WindowGroupManager] Minimized window: %s", view->appId().c_str());
}

void WindowGroupManager::maximizeWindow(View* view) {
    if (!view) return;
    
    WindowState& state = m_windowStates[view];
    
    if (!state.maximized) {
        // Save current geometry for restore
        state.restoreGeometry = view->geom();
        state.maximized = true;
        
        // Would maximize window to output size
        LOG_INFO("[WindowGroupManager] Maximized window: %s", view->appId().c_str());
    } else {
        // Restore
        state.maximized = false;
        // Would restore geometry
        LOG_INFO("[WindowGroupManager] Restored window: %s", view->appId().c_str());
    }
}

void WindowGroupManager::fullscreenWindow(View* view, bool fullscreen) {
    if (!view) return;
    
    m_windowStates[view].fullscreen = fullscreen;
    LOG_INFO("[WindowGroupManager] Fullscreen %s: %s", 
             fullscreen ? "enabled" : "disabled", view->appId().c_str());
}

void WindowGroupManager::toggleAlwaysOnTop(View* view) {
    if (!view) return;
    
    WindowState& state = m_windowStates[view];
    state.alwaysOnTop = !state.alwaysOnTop;
    
    LOG_INFO("[WindowGroupManager] Always on top %s: %s",
             state.alwaysOnTop ? "enabled" : "disabled", view->appId().c_str());
}

void WindowGroupManager::toggleSticky(View* view) {
    if (!view) return;
    
    WindowState& state = m_windowStates[view];
    state.sticky = !state.sticky;
    
    LOG_INFO("[WindowGroupManager] Sticky %s: %s",
             state.sticky ? "enabled" : "disabled", view->appId().c_str());
}

void WindowGroupManager::closeWindow(View* view) {
    if (!view) return;
    
    LOG_INFO("[WindowGroupManager] Closing window: %s", view->appId().c_str());
    
    if (m_onWindowClose) {
        m_onWindowClose(view);
    }
    
    // Remove from any group
    removeWindowFromGroup(view);
    
    // Clear state
    m_windowStates.erase(view);
    
    if (m_focusedWindow == view) {
        m_focusedWindow = nullptr;
    }
}

bool WindowGroupManager::isMinimized(View* view) const {
    auto it = m_windowStates.find(view);
    return it != m_windowStates.end() && it->second.minimized;
}

bool WindowGroupManager::isMaximized(View* view) const {
    auto it = m_windowStates.find(view);
    return it != m_windowStates.end() && it->second.maximized;
}

bool WindowGroupManager::isFullscreen(View* view) const {
    auto it = m_windowStates.find(view);
    return it != m_windowStates.end() && it->second.fullscreen;
}

bool WindowGroupManager::isAlwaysOnTop(View* view) const {
    auto it = m_windowStates.find(view);
    return it != m_windowStates.end() && it->second.alwaysOnTop;
}

bool WindowGroupManager::isSticky(View* view) const {
    auto it = m_windowStates.find(view);
    return it != m_windowStates.end() && it->second.sticky;
}

void WindowGroupManager::focusWindow(View* view) {
    if (!view) return;
    
    m_focusedWindow = view;
    
    LOG_DEBUG("[WindowGroupManager] Focused window: %s", view->appId().c_str());
    
    if (m_onWindowFocus) {
        m_onWindowFocus(view);
    }
}

std::vector<View*> WindowGroupManager::getAllWindows() const {
    std::vector<View*> allWindows;
    
    for (const auto& group : m_groups) {
        for (View* view : group->windows) {
            if (view) {
                allWindows.push_back(view);
            }
        }
    }
    
    // Also include ungrouped windows (would need access to all views)
    // This is a simplified version
    
    return allWindows;
}

std::vector<View*> WindowGroupManager::getVisibleWindows() const {
    std::vector<View*> visible;
    
    for (const auto& group : m_groups) {
        if (group->type == GroupType::Tabbed) {
            // Only active window visible
            if (group->activeIndex >= 0 && 
                group->activeIndex < static_cast<int>(group->windows.size())) {
                visible.push_back(group->windows[group->activeIndex]);
            }
        } else {
            // All windows visible
            for (View* view : group->windows) {
                if (view && !isMinimized(view)) {
                    visible.push_back(view);
                }
            }
        }
    }
    
    return visible;
}

std::vector<View*> WindowGroupManager::getWindowsOnWorkspace(uint32_t workspace) const {
    std::vector<View*> result;
    
    for (const auto& group : m_groups) {
        if (group->workspace == workspace) {
            for (View* view : group->windows) {
                if (view && !isMinimized(view)) {
                    result.push_back(view);
                }
            }
        }
    }
    
    return result;
}

void WindowGroupManager::updateGroupGeometry(uint64_t groupId) {
    WindowGroup* group = getGroup(groupId);
    if (!group) return;
    
    // Calculate bounding box of all windows
    if (group->windows.empty()) return;
    
    Rect bounds = group->windows[0]->geom();
    
    for (size_t i = 1; i < group->windows.size(); i++) {
        View* view = group->windows[i];
        Rect geom = view->geom();
        
        int x1 = std::min(bounds.x, geom.x);
        int y1 = std::min(bounds.y, geom.y);
        int x2 = std::max(bounds.x + bounds.w, geom.x + geom.w);
        int y2 = std::max(bounds.y + bounds.h, geom.y + geom.h);
        
        bounds.x = x1;
        bounds.y = y1;
        bounds.w = x2 - x1;
        bounds.h = y2 - y1;
    }
    
    group->geometry = bounds;
}

void WindowGroupManager::arrangeGroupWindows(uint64_t groupId) {
    WindowGroup* group = getGroup(groupId);
    if (!group || group->windows.empty()) return;
    
    switch (group->type) {
        case GroupType::Tabbed:
            // All windows same position, only active visible
            // Would set all to same geometry
            break;
            
        case GroupType::Stacked:
            // Stack vertically with title bars
            // Would arrange in vertical stack
            break;
            
        case GroupType::SplitH:
            // Split horizontally based on ratio
            // Would arrange side by side
            break;
            
        case GroupType::SplitV:
            // Split vertically based on ratio
            // Would arrange top/bottom
            break;
            
        case GroupType::Grid:
            // Arrange in grid
            // Would calculate grid positions
            break;
            
        default:
            break;
    }
    
    updateGroupGeometry(groupId);
}

} // namespace havel
