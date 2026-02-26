// Notifications Plugin - demonstrates view map hook capability
// Shows how to track windows and show notifications

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

namespace havel {

/**
 * Notifications Plugin
 * 
 * Demonstrates view lifecycle hooks for notifications.
 * Actual implementation requires:
 * - Notification daemon integration (e.g., mako, dunst)
 * - On-screen notification rendering
 * - Notification queue management
 * 
 * This stub shows the plugin structure.
 */
class NotificationsPlugin : public Plugin {
public:
    const char* name() const override { return "notifications"; }
    const char* version() const override { return "0.1.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        m_notificationCount = 0;
        printf("[NotificationsPlugin] Initialized\n");
    }
    
    void fini() override {
        printf("[NotificationsPlugin] Finalized (%lu notifications tracked)\n",
               m_notificationCount);
        m_api = nullptr;
    }
    
    void onViewMap(const ViewEvent& event) override {
        // Track when new windows open
        const char* appId = event.appId ? event.appId : "unknown";
        printf("[NotificationsPlugin] App mapped: %s\n", appId);
        
        // Could show notification for certain apps
        if (strcmp(appId, "firefox") == 0 || strcmp(appId, "chromium") == 0) {
            showNotification("Browser opened", appId);
        }
    }
    
    void onViewDestroy(const ViewEvent& event) override {
        // Track when windows close
        std::string appId = event.appId ? event.appId : "unknown";
        printf("[NotificationsPlugin] App closed: %s\n", appId.c_str());
    }
    
    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        
        // Meta+N shows notification test
        if (event.pressed && (event.modifiers & MOD_LOGO) && event.keycode == 39) {
            showNotification("Test Notification", "NotificationsPlugin");
            return true;
        }
        
        return false;
    }
    
private:
    CompositorAPI* m_api = nullptr;
    size_t m_notificationCount;
    
    void showNotification(const char* title, const char* app) {
        m_notificationCount++;
        printf("[NotificationsPlugin] [%lu] %s: %s\n",
               m_notificationCount, app, title);
        
        // Actual implementation would:
        // 1. Create notification object
        // 2. Add to notification queue
        // 3. Render on-screen (top-right corner)
        // 4. Auto-dismiss after timeout
        // 5. Handle click actions
    }
};

// Plugin factory
Plugin* create_notifications_plugin() {
    return new NotificationsPlugin();
}

} // namespace havel
