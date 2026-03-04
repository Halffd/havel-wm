// XDG Desktop Portal Integration - Screen sharing portal for wlroots
// Implements org.freedesktop.portal.ScreenCast interface

#pragma once

#include <wayland-server-core.h>
#include <gio/gio.h>
#include <string>
#include <functional>
#include <vector>
#include <memory>

namespace havel {

/**
 * XDG Desktop Portal - ScreenCast Integration
 * 
 * Provides screen sharing portal for:
 * - Firefox (WebRTC)
 * - Chrome/Chromium (WebRTC)
 * - Electron apps (Discord, Slack, etc.)
 * - Any app using xdg-desktop-portal
 * 
 * This integrates with xdg-desktop-portal-wlr which handles:
 * - D-Bus portal communication
 * - Screen selection UI
 * - PipeWire stream creation
 * - wlr-screencopy integration
 * 
 * Features:
 * - Multiple monitor selection
 * - Window selection (future)
 * - Cursor compositing option
 * - Audio source selection (future)
 */
class DesktopPortal {
public:
    DesktopPortal();
    ~DesktopPortal();

    // Initialize portal integration
    bool initialize(struct wl_display* display);
    void shutdown();

    // Portal state
    bool isAvailable() const { return m_available; }
    bool isActive() const { return m_active; }

    // Screen sharing session
    struct Session {
        std::string sessionId;
        std::vector<std::string> streams;  // PipeWire node IDs
        bool withCursor = false;
        bool active = false;
    };

    // Get active sessions
    const std::vector<Session>& getSessions() const { return m_sessions; }

    // Callbacks
    using SessionCallback = std::function<void(const Session&, bool started)>;
    void setSessionCallback(SessionCallback callback) { 
        m_sessionCallback = callback; 
    }

private:
    struct wl_display* m_display = nullptr;
    GDBusConnection* m_dbusConnection = nullptr;
    bool m_available = false;
    bool m_active = false;
    bool m_initialized = false;

    std::vector<Session> m_sessions;
    SessionCallback m_sessionCallback;

    // D-Bus method handlers
    static void onPortalMethodCall(
        GDBusConnection* connection,
        const gchar* sender,
        const gchar* objectPath,
        const gchar* interfaceName,
        const gchar* methodName,
        GVariant* parameters,
        GDBusMethodInvocation* invocation,
        gpointer userData);

    // Portal methods
    void handleCreateSession(GDBusMethodInvocation* invocation, GVariant* params);
    void handleSelectSources(GDBusMethodInvocation* invocation, GVariant* params);
    void handleStart(GDBusMethodInvocation* invocation, GVariant* params);

    // D-Bus signal handlers
    static void onNameAppeared(GDBusConnection* connection,
                                const gchar* name,
                                const gchar* nameOwner,
                                gpointer userData);
    static void onNameVanished(GDBusConnection* connection,
                                const gchar* name,
                                gpointer userData);

    // Helper methods
    bool registerPortalInterface();
    void unregisterPortalInterface();
    void emitSessionStarted(const std::string& sessionId, GVariant* streams);
    void emitSessionStopped(const std::string& sessionId);

    // Portal properties
    guint m_registrationId = 0;
    guint m_nameWatcherId = 0;

    static const gchar* s_introspectionXml;
};

/**
 * Portal Manager - Singleton for portal access
 */
class PortalManager {
public:
    static PortalManager& getInstance();

    bool initialize(struct wl_display* display);
    void shutdown();

    DesktopPortal* getScreenCastPortal() { return m_screenCastPortal.get(); }
    
    bool isAvailable() const { return m_available; }

private:
    PortalManager() = default;
    ~PortalManager() { shutdown(); }

    std::unique_ptr<DesktopPortal> m_screenCastPortal;
    bool m_available = false;
};

} // namespace havel
