// Notifications Plugin - On-screen notifications with overlay rendering
// Shows notifications from D-Bus org.freedesktop.Notifications interface

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <wm/core/NotificationDaemon.hpp>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <chrono>

namespace havel {

// Get current time in milliseconds
static uint64_t getMonotonicTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

/**
 * Notification structure
 */
struct Notification {
    std::string title;
    std::string body;
    std::string app;
    uint64_t createdTime;
    uint64_t timeout;  // ms, 0 = persistent
    bool dismissed;

    Notification(const std::string& t, const std::string& b, const std::string& a, uint64_t to = 5000)
        : title(t), body(b), app(a), createdTime(getMonotonicTimeMs()), timeout(to), dismissed(false) {}

    bool isExpired() const {
        if (timeout == 0) return false;
        return (getMonotonicTimeMs() - createdTime) > timeout + 500;  // Add fade time
    }

    float getFadeAlpha() const {
        if (timeout == 0) return 1.0f;
        uint64_t elapsed = getMonotonicTimeMs() - createdTime;
        uint64_t remaining = timeout - elapsed;
        if (remaining > 1000) return 1.0f;
        return static_cast<float>(remaining) / 1000.0f;
    }
};

/**
 * Notifications Plugin
 *
 * Displays on-screen notifications for:
 * - Application events (new windows, etc.)
 * - System alerts
 * - Custom messages via keybinding
 *
 * Features:
 * - Queue management
 * - Auto-dismiss with timeout
 * - Visual rendering via overlay
 * - Fade in/out animations
 */
class NotificationsPlugin : public Plugin {
public:
    const char* name() const override { return "notifications"; }
    const char* version() const override { return "0.3.0"; }

    void init(CompositorAPI* api) override {
        m_api = api;
        m_maxNotifications = 5;
        m_notificationWidth = 380;
        m_notificationHeight = 90;
        m_margin = 16;
        m_cornerRadius = 8;
        
        // Initialize D-Bus notification daemon
        m_daemon = std::make_unique<DbusNotificationDaemon>();
        if (m_daemon->initialize()) {
            // Set callback to receive notifications from D-Bus
            m_daemon->setDbusNotificationCallback([this](const DbusNotification& notif) {
                onExternalNotification(notif);
            });
            printf("[NotificationsPlugin] D-Bus notification daemon running\n");
        } else {
            printf("[NotificationsPlugin] Failed to start D-Bus daemon\n");
        }
        
        printf("[NotificationsPlugin] Initialized (max %d notifications)\n", m_maxNotifications);
    }

    void fini() override {
        if (m_daemon) {
            m_daemon->shutdown();
            m_daemon.reset();
        }
        printf("[NotificationsPlugin] Finalized (%lu notifications shown)\n",
               m_totalNotifications);
        m_api = nullptr;
    }

    void loadConfig(const std::string& configPath) override {
        (void)configPath;
        printf("[NotificationsPlugin] Config loaded\n");
    }

    void onViewMap(const ViewEvent& event) override {
        const char* appId = event.appId ? event.appId : "unknown";

        // Show notification for certain apps
        if (strcmp(appId, "firefox") == 0 || strcmp(appId, "chromium") == 0 ||
            strcmp(appId, "Google-chrome") == 0) {
            showNotification("Browser Launched", appId, "A new browser window has opened.");
        } else if (strcmp(appId, "foot") == 0 || strcmp(appId, "alacritty") == 0 ||
                   strcmp(appId, "kitty") == 0 || strcmp(appId, "wezterm") == 0) {
            showNotification("Terminal Launched", appId, "A new terminal has opened.");
        } else if (strcmp(appId, "code") == 0 || strcmp(appId, "sublime_text") == 0) {
            showNotification("Editor Launched", appId, "Happy coding!");
        }
    }

    void onViewDestroy(const ViewEvent& event) override {
        (void)event;
    }

    void onOutputFrame(const OutputFrameEvent& event) override {
        cleanupExpired();
        m_lastFrameEvent = event;
    }

    void renderOverlay(void* rendererPtr) override {
        if (m_notifications.empty() || !rendererPtr) return;

        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
        int width = m_lastFrameEvent.width;
        int height = m_lastFrameEvent.height;

        // Render from top-right corner, stacking downward
        int currentY = m_margin;
        int visibleCount = 0;

        for (auto& notif : m_notifications) {
            if (notif.dismissed) continue;
            if (visibleCount >= m_maxNotifications) break;

            float alpha = notif.getFadeAlpha();
            if (alpha <= 0.01f) {
                currentY += (m_notificationHeight + m_margin);
                continue;
            }

            int x = width - m_margin - m_notificationWidth;
            int y = currentY;

            // Draw notification background
            renderer->drawRect(x, y, m_notificationWidth, m_notificationHeight,
                              Color(0.15f, 0.15f, 0.2f, 0.95f * alpha));

            // Draw accent bar on left
            renderer->drawRect(x, y, 4, m_notificationHeight,
                              Color(0.3f, 0.6f, 1.0f, alpha));

            // Draw title
            renderer->drawText(notif.title.c_str(), x + 12, y + 18, 14.0f,
                              Color(1.0f, 1.0f, 1.0f, alpha));

            // Draw body (truncated if too long)
            std::string bodyText = notif.body;
            if (bodyText.length() > 45) {
                bodyText = bodyText.substr(0, 42) + "...";
            }
            renderer->drawText(bodyText.c_str(), x + 12, y + 42, 11.0f,
                              Color(0.8f, 0.8f, 0.8f, alpha));

            // Draw app name (small, bottom right)
            renderer->drawText(notif.app.c_str(), x + m_notificationWidth - 70, y + 68,
                              9.0f, Color(0.6f, 0.6f, 0.6f, alpha));

            currentY += (m_notificationHeight + m_margin);
            visibleCount++;

            if (currentY + m_notificationHeight > height) break;
        }
    }

    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        constexpr uint32_t MOD_SHIFT = 1 << 1;

        if (!event.pressed) return false;
        if (!(event.modifiers & MOD_LOGO)) return false;

        // Meta+N shows test notification
        if (event.keycode == 39) {  // N
            if (event.modifiers & MOD_SHIFT) {
                clearAll();
            } else {
                showNotification("Test Notification", "NotificationsPlugin",
                               "This is a test notification. Press Meta+Shift+N to clear.");
            }
            return true;
        }

        return false;
    }

    // Public API for showing notifications
    void showNotification(const char* title, const char* app, const char* body = nullptr, 
                          uint64_t timeout = 5000) {
        Notification notif(title, body ? body : "", app, timeout);

        while (m_notifications.size() >= static_cast<size_t>(m_maxNotifications)) {
            m_notifications.erase(m_notifications.begin());
        }

        m_notifications.push_back(notif);
        m_totalNotifications++;

        printf("[NotificationsPlugin] [%lu] %s: %s\n", m_totalNotifications, app, title);
        if (body && strlen(body) > 0) {
            printf("                 %s\n", body);
        }
    }

private:
    CompositorAPI* m_api = nullptr;
    std::unique_ptr<DbusNotificationDaemon> m_daemon;  // D-Bus notification daemon
    std::vector<Notification> m_notifications;
    size_t m_totalNotifications = 0;
    OutputFrameEvent m_lastFrameEvent;
    
    int m_maxNotifications;
    int m_notificationWidth;
    int m_notificationHeight;
    int m_margin;
    int m_cornerRadius;

    void clearAll() {
        m_notifications.clear();
        printf("[NotificationsPlugin] Cleared all notifications\n");
    }

    void cleanupExpired() {
        auto it = m_notifications.begin();
        while (it != m_notifications.end()) {
            if (it->isExpired()) {
                it = m_notifications.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // Handle notifications from D-Bus daemon
    void onExternalNotification(const havel::DbusNotification& notif) {
        // Create internal notification from external one
        m_notifications.emplace_back(notif.summary, notif.body, notif.app, (uint64_t)notif.timeout);
        m_totalNotifications++;
        
        printf("[NotificationsPlugin] External notification: %s - %s\n", 
               notif.summary.c_str(), notif.body.c_str());
    }
};

// Plugin factory
Plugin* create_notifications_plugin() {
    return new NotificationsPlugin();
}

} // namespace havel
