// XDG Desktop Portal Integration Implementation
// Screen sharing portal for wlroots compositors

#include "DesktopPortal.hpp"
#include "PipeWireStream.hpp"
#include <Logger.h>
#include <cstdio>
#include <cstring>

namespace havel {

// D-Bus introspection XML for ScreenCast portal
const gchar* DesktopPortal::s_introspectionXml = R"(
<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
                      "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node>
  <interface name="org.freedesktop.portal.ScreenCast">
    <method name="CreateSession">
      <arg type="a{sv}" name="options" direction="in"/>
      <arg type="o" name="handle" direction="out"/>
    </method>
    <method name="SelectSources">
      <arg type="o" name="session_handle" direction="in"/>
      <arg type="a{sv}" name="options" direction="in"/>
      <arg type="o" name="handle" direction="out"/>
    </method>
    <method name="Start">
      <arg type="o" name="session_handle" direction="in"/>
      <arg type="a{sv}" name="options" direction="in"/>
      <arg type="o" name="handle" direction="out"/>
    </method>
    <property name="AvailableSourceTypes" type="u" access="read"/>
    <property name="AvailableCursorModes" type="u" access="read"/>
    <property name="version" type="u" access="read"/>
  </interface>
</node>
)";

DesktopPortal::DesktopPortal() = default;

DesktopPortal::~DesktopPortal() {
    shutdown();
}

bool DesktopPortal::initialize(struct wl_display* display) {
    if (m_initialized) return true;

    m_display = display;

    // Get D-Bus connection
    GError* error = nullptr;
    m_dbusConnection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    
    if (!m_dbusConnection) {
        LOG_ERROR("[Portal] Failed to connect to D-Bus: %s", error->message);
        g_error_free(error);
        return false;
    }

    // Watch for xdg-desktop-portal name
    m_nameWatcherId = g_bus_watch_name(
        G_BUS_TYPE_SESSION,
        "org.freedesktop.portal.Desktop",
        G_BUS_NAME_WATCHER_FLAGS_NONE,
        onNameAppeared,
        onNameVanished,
        this,
        nullptr
    );

    m_initialized = true;
    LOG_INFO("[Portal] Desktop portal integration initialized");
    return true;
}

void DesktopPortal::shutdown() {
    if (m_nameWatcherId > 0) {
        g_bus_unwatch_name(m_nameWatcherId);
        m_nameWatcherId = 0;
    }

    if (m_registrationId > 0) {
        g_dbus_connection_unregister_object(m_dbusConnection, m_registrationId);
        m_registrationId = 0;
    }

    if (m_dbusConnection) {
        g_object_unref(m_dbusConnection);
        m_dbusConnection = nullptr;
    }

    m_sessions.clear();
    m_available = false;
    m_active = false;
    m_initialized = false;

    LOG_INFO("[Portal] Desktop portal shutdown complete");
}

void DesktopPortal::onNameAppeared(GDBusConnection* connection,
                                    const gchar* name,
                                    const gchar* nameOwner,
                                    gpointer userData) {
    DesktopPortal* portal = static_cast<DesktopPortal*>(userData);
    
    LOG_INFO("[Portal] xdg-desktop-portal appeared: %s", nameOwner);
    portal->m_available = true;

    // Register our ScreenCast interface
    if (portal->registerPortalInterface()) {
        LOG_INFO("[Portal] ScreenCast interface registered");
    }
}

void DesktopPortal::onNameVanished(GDBusConnection* connection,
                                    const gchar* name,
                                    gpointer userData) {
    DesktopPortal* portal = static_cast<DesktopPortal*>(userData);
    
    LOG_WARN("[Portal] xdg-desktop-portal vanished");
    portal->m_available = false;
    portal->m_active = false;

    // Unregister interface
    portal->unregisterPortalInterface();
}

bool DesktopPortal::registerPortalInterface() {
    if (!m_dbusConnection) return false;

    GError* error = nullptr;
    GDBusInterfaceVTable vtable = {
        onPortalMethodCall,
        nullptr,  // get_property
        nullptr,  // set_property
        {nullptr} // padding
    };

    GDBusNodeInfo* introspectionData = 
        g_dbus_node_info_new_for_xml(s_introspectionXml, &error);
    
    if (!introspectionData) {
        LOG_ERROR("[Portal] Failed to parse introspection XML: %s", error->message);
        g_error_free(error);
        return false;
    }

    m_registrationId = g_dbus_connection_register_object(
        m_dbusConnection,
        "/org/freedesktop/portal/desktop",
        introspectionData->interfaces[0],
        &vtable,
        this,
        nullptr,
        &error
    );

    g_dbus_node_info_unref(introspectionData);

    if (m_registrationId == 0) {
        LOG_ERROR("[Portal] Failed to register object: %s", error->message);
        g_error_free(error);
        return false;
    }

    LOG_INFO("[Portal] ScreenCast interface registered on D-Bus");
    return true;
}

void DesktopPortal::unregisterPortalInterface() {
    if (m_registrationId > 0) {
        g_dbus_connection_unregister_object(m_dbusConnection, m_registrationId);
        m_registrationId = 0;
        LOG_INFO("[Portal] ScreenCast interface unregistered");
    }
}

void DesktopPortal::onPortalMethodCall(
    GDBusConnection* connection,
    const gchar* sender,
    const gchar* objectPath,
    const gchar* interfaceName,
    const gchar* methodName,
    GVariant* parameters,
    GDBusMethodInvocation* invocation,
    gpointer userData) 
{
    DesktopPortal* portal = static_cast<DesktopPortal*>(userData);

    LOG_DEBUG("[Portal] Method call: %s from %s", methodName, sender);

    if (strcmp(methodName, "CreateSession") == 0) {
        portal->handleCreateSession(invocation, parameters);
    } else if (strcmp(methodName, "SelectSources") == 0) {
        portal->handleSelectSources(invocation, parameters);
    } else if (strcmp(methodName, "Start") == 0) {
        portal->handleStart(invocation, parameters);
    } else {
        g_dbus_method_invocation_return_error(
            invocation,
            G_DBUS_ERROR,
            G_DBUS_ERROR_UNKNOWN_METHOD,
            "Unknown method %s", methodName);
    }
}

void DesktopPortal::handleCreateSession(GDBusMethodInvocation* invocation, 
                                         GVariant* params) {
    LOG_INFO("[Portal] CreateSession called");

    // Generate session handle
    std::string sessionHandle = "/org/freedesktop/portal/desktop/session/" + 
                                 std::to_string(g_get_monotonic_time());

    // Create session
    Session session;
    session.sessionId = sessionHandle;
    session.active = false;
    m_sessions.push_back(std::move(session));

    // Return handle
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    
    GVariant* result = g_variant_new("(o@a{sv})", 
                                      sessionHandle.c_str(),
                                      g_variant_builder_end(&builder));
    
    g_dbus_method_invocation_return_value(invocation, result);
    LOG_INFO("[Portal] Session created: %s", sessionHandle.c_str());
}

void DesktopPortal::handleSelectSources(GDBusMethodInvocation* invocation,
                                         GVariant* params) {
    LOG_INFO("[Portal] SelectSources called");

    // In a full implementation, this would show a UI for source selection
    // For now, we accept all sources

    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    
    GVariant* result = g_variant_new("(o@a{sv})",
                                      "",
                                      g_variant_builder_end(&builder));
    
    g_dbus_method_invocation_return_value(invocation, result);
    LOG_INFO("[Portal] Sources selected");
}

void DesktopPortal::handleStart(GDBusMethodInvocation* invocation,
                                 GVariant* params) {
    LOG_INFO("[Portal] Start called");

    // Get session handle from parameters
    const gchar* sessionHandle;
    g_variant_get(params, "(&o@a{sv})", &sessionHandle, nullptr);

    // Find session
    Session* session = nullptr;
    for (auto& s : m_sessions) {
        if (s.sessionId == sessionHandle) {
            session = &s;
            break;
        }
    }

    if (!session) {
        g_dbus_method_invocation_return_error(
            invocation,
            G_DBUS_ERROR,
            G_DBUS_ERROR_UNKNOWN_OBJECT,
            "Session not found: %s", sessionHandle);
        return;
    }

    // Create PipeWire stream for screen sharing
    // In production, would enumerate outputs and create streams
    
    // For now, return success with empty streams
    // Full implementation would:
    // 1. Enumerate available outputs
    // 2. Create PipeWire streams via PipeWireManager
    // 3. Return stream node IDs to caller

    GVariantBuilder streamsBuilder;
    g_variant_builder_init(&streamsBuilder, G_VARIANT_TYPE_ARRAY);
    
    GVariantBuilder optionsBuilder;
    g_variant_builder_init(&optionsBuilder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&optionsBuilder, "{sv}", "streams", 
                          g_variant_builder_end(&streamsBuilder));

    GVariant* result = g_variant_new("(u@a{sv})",
                                      0,  // Success
                                      g_variant_builder_end(&optionsBuilder));

    g_dbus_method_invocation_return_value(invocation, result);

    session->active = true;
    m_active = true;

    LOG_INFO("[Portal] Screen sharing started for session: %s", sessionHandle);

    if (m_sessionCallback) {
        m_sessionCallback(*session, true);
    }
}

void DesktopPortal::emitSessionStarted(const std::string& sessionId,
                                        GVariant* streams) {
    // Emit SessionStarted signal
    GVariantBuilder propsBuilder;
    g_variant_builder_init(&propsBuilder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&propsBuilder, "{sv}", "streams", streams);

    GVariant* signal = g_variant_new("(o@a{sv})",
                                      sessionId.c_str(),
                                      g_variant_builder_end(&propsBuilder));

    g_dbus_connection_emit_signal(
        m_dbusConnection,
        nullptr,  // destination (broadcast)
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.ScreenCast",
        "SessionStarted",
        signal,
        nullptr);
}

void DesktopPortal::emitSessionStopped(const std::string& sessionId) {
    // Emit SessionStopped signal
    GVariant* signal = g_variant_new("(o)", sessionId.c_str());

    g_dbus_connection_emit_signal(
        m_dbusConnection,
        nullptr,
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.ScreenCast",
        "SessionStopped",
        signal,
        nullptr);
}

// PortalManager implementation
PortalManager& PortalManager::getInstance() {
    static PortalManager instance;
    return instance;
}

bool PortalManager::initialize(struct wl_display* display) {
    if (m_available) return true;

    m_screenCastPortal = std::make_unique<DesktopPortal>();
    if (!m_screenCastPortal->initialize(display)) {
        return false;
    }

    m_available = true;
    LOG_INFO("[Portal] Portal manager initialized");
    return true;
}

void PortalManager::shutdown() {
    if (m_screenCastPortal) {
        m_screenCastPortal->shutdown();
        m_screenCastPortal.reset();
    }
    m_available = false;
    LOG_INFO("[Portal] Portal manager shutdown");
}

} // namespace havel
