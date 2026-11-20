// DbusNotification Daemon - D-Bus notification server
// Implements org.freedesktop.DbusNotifications specification

#pragma once

#include <wm/plugins/Plugin.hpp>
#include <string>
#include <vector>
#include <functional>

// Forward declare GLib types (avoid including glib.h in header)
typedef struct _GDBusConnection GDBusConnection;
typedef struct _GVariant GVariant;
typedef struct _GDBusMethodInvocation GDBusMethodInvocation;
typedef char gchar;
typedef unsigned int guint;
typedef void* gpointer;
typedef const void* gconstpointer;

namespace havel {

/**
 * D-Bus notification data structure
 */
struct DbusNotification {
    uint32_t id = 0;
    std::string app;
    std::string summary;
    std::string body;
    std::vector<std::string> actions;  // Action identifiers
    int timeout = 5000;  // ms, 0 = never expire
    uint64_t timestamp = 0;
    bool dismissed = false;
};

/**
 * DbusNotification callback type
 */
using DbusNotificationCallback = std::function<void(const DbusNotification&)>;

/**
 * D-Bus DbusNotification Daemon
 * 
 * Implements org.freedesktop.DbusNotifications interface:
 * https://specifications.freedesktop.org/notification-spec/
 * 
 * Usage from applications:
 *   dbus-send --session --type=method_call \
 *     --dest=org.freedesktop.DbusNotifications \
 *     /org/freedesktop/DbusNotifications \
 *     org.freedesktop.DbusNotifications.Notify \
 *     string:"app" uint32:0 string:"" string:"Summary" string:"Body" \
 *     array:string: array:dict:string:variant: int32:5000
 */
class DbusNotificationDaemon {
public:
    DbusNotificationDaemon();
    ~DbusNotificationDaemon();
    
    // Initialize D-Bus service
    bool initialize();
    void shutdown();
    bool isRunning() const { return m_running; }
    
    // Show notification (called from D-Bus handler)
    uint32_t showDbusNotification(
        const std::string& app,
        const std::string& summary,
        const std::string& body,
        const std::vector<std::string>& actions,
        int timeout
    );
    
    // Close notification programmatically
    void closeDbusNotification(uint32_t id);
    
    // Get server information
    std::string getServerName() const { return "Havel WM"; }
    std::string getServerVendor() const { return "Havel Project"; }
    std::string getServerVersion() const { return "0.1.0"; }
    std::string getSpecVersion() const { return "1.2"; }
    
    // Set callback for when notifications are shown
    void setDbusNotificationCallback(DbusNotificationCallback callback) {
        m_onDbusNotification = callback;
    }
    
private:
    // D-Bus method handlers
    void handleNotify(GDBusMethodInvocation* invocation, GVariant* params);
    void handleCloseDbusNotification(GDBusMethodInvocation* invocation, GVariant* params);
    void handleGetCapabilities(GDBusMethodInvocation* invocation);
    void handleGetServerInformation(GDBusMethodInvocation* invocation);
    
    // Static D-Bus method dispatcher
    static void onMethodCall(
        GDBusConnection* connection,
        const gchar* sender,
        const gchar* objectPath,
        const gchar* interfaceName,
        const gchar* methodName,
        GVariant* parameters,
        GDBusMethodInvocation* invocation,
        gpointer userData
    );
    
    GDBusConnection* m_connection = nullptr;
    guint m_registrationId = 0;
    bool m_running = false;
    uint32_t m_nextId = 1;
    std::vector<DbusNotification> m_notifications;
    DbusNotificationCallback m_onDbusNotification;
    
    // D-Bus introspection XML
    static const gchar* s_introspectionXml;
};

} // namespace havel
