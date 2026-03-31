// DbusNotification Daemon - D-Bus notification server implementation
// Implements org.freedesktop.DbusNotifications specification

#include "NotificationDaemon.hpp"
#include <Logger.h>
#include <gio/gio.h>
#include <chrono>

namespace havel {

// D-Bus introspection XML for DbusNotifications interface
const gchar* DbusNotificationDaemon::s_introspectionXml = R"(
<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
                      "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node>
  <interface name="org.freedesktop.DbusNotifications">
    <method name="Notify">
      <arg type="s" name="app_name" direction="in"/>
      <arg type="u" name="replaces_id" direction="in"/>
      <arg type="s" name="app_icon" direction="in"/>
      <arg type="s" name="summary" direction="in"/>
      <arg type="s" name="body" direction="in"/>
      <arg type="as" name="actions" direction="in"/>
      <arg type="a{sv}" name="hints" direction="in"/>
      <arg type="i" name="expire_timeout" direction="in"/>
      <arg type="u" name="id" direction="out"/>
    </method>
    <method name="CloseDbusNotification">
      <arg type="u" name="id" direction="in"/>
    </method>
    <method name="GetCapabilities">
      <arg type="as" name="capabilities" direction="out"/>
    </method>
    <method name="GetServerInformation">
      <arg type="s" name="name" direction="out"/>
      <arg type="s" name="vendor" direction="out"/>
      <arg type="s" name="version" direction="out"/>
      <arg type="s" name="spec_version" direction="out"/>
    </method>
    <signal name="DbusNotificationClosed">
      <arg type="u" name="id"/>
      <arg type="u" name="reason"/>
    </signal>
    <signal name="ActionInvoked">
      <arg type="u" name="id"/>
      <arg type="s" name="action_key"/>
    </signal>
  </interface>
</node>
)";

DbusNotificationDaemon::DbusNotificationDaemon() = default;

DbusNotificationDaemon::~DbusNotificationDaemon() {
    shutdown();
}

bool DbusNotificationDaemon::initialize() {
    if (m_running) return true;
    
    LOG_INFO("[DbusNotificationDaemon] Initializing D-Bus service");
    
    GError* error = nullptr;
    
    // Connect to session bus
    m_connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!m_connection) {
        LOG_ERROR("[DbusNotificationDaemon] Failed to connect to D-Bus: %s", error->message);
        g_error_free(error);
        return false;
    }
    
    // Register object
    GDBusInterfaceVTable vtable = {
        onMethodCall,
        nullptr,  // get_property
        nullptr   // set_property
    };
    
    GDBusNodeInfo* introspectionData = g_dbus_node_info_new_for_xml(s_introspectionXml, &error);
    if (!introspectionData) {
        LOG_ERROR("[DbusNotificationDaemon] Failed to parse introspection XML: %s", error->message);
        g_error_free(error);
        g_object_unref(m_connection);
        m_connection = nullptr;
        return false;
    }
    
    m_registrationId = g_dbus_connection_register_object(
        m_connection,
        "/org/freedesktop/DbusNotifications",
        introspectionData->interfaces[0],
        &vtable,
        this,  // user_data
        nullptr,  // user_data_free_func
        &error
    );
    
    g_dbus_node_info_unref(introspectionData);
    
    if (!m_registrationId) {
        LOG_ERROR("[DbusNotificationDaemon] Failed to register object: %s", error->message);
        g_error_free(error);
        g_object_unref(m_connection);
        m_connection = nullptr;
        return false;
    }
    
    // Request well-known name
    guint ownerId = g_bus_own_name_on_connection(
        m_connection,
        "org.freedesktop.DbusNotifications",
        G_BUS_NAME_OWNER_FLAGS_NONE,
        nullptr,  // name_acquired
        nullptr,  // name_lost
        nullptr,  // user_data
        nullptr   // user_data_free_func
    );
    
    if (ownerId == 0) {
        LOG_ERROR("[DbusNotificationDaemon] Failed to own name");
        g_dbus_connection_unregister_object(m_connection, m_registrationId);
        g_object_unref(m_connection);
        m_connection = nullptr;
        return false;
    }
    
    m_running = true;
    LOG_INFO("[DbusNotificationDaemon] D-Bus service running as org.freedesktop.DbusNotifications");
    
    return true;
}

void DbusNotificationDaemon::shutdown() {
    if (!m_running) return;
    
    LOG_INFO("[DbusNotificationDaemon] Shutting down D-Bus service");
    
    if (m_registrationId > 0) {
        g_dbus_connection_unregister_object(m_connection, m_registrationId);
        m_registrationId = 0;
    }
    
    if (m_connection) {
        g_object_unref(m_connection);
        m_connection = nullptr;
    }
    
    m_running = false;
}

uint32_t DbusNotificationDaemon::showDbusNotification(
    const std::string& app,
    const std::string& summary,
    const std::string& body,
    const std::vector<std::string>& actions,
    int timeout
) {
    DbusNotification notif;
    notif.id = m_nextId++;
    notif.app = app;
    notif.summary = summary;
    notif.body = body;
    notif.actions = actions;
    notif.timeout = timeout;
    notif.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    
    m_notifications.push_back(notif);
    
    LOG_INFO("[DbusNotificationDaemon] DbusNotification %u: %s - %s", notif.id, summary.c_str(), body.c_str());
    
    // Call callback if set
    if (m_onDbusNotification) {
        m_onDbusNotification(notif);
    }
    
    return notif.id;
}

void DbusNotificationDaemon::closeDbusNotification(uint32_t id) {
    for (auto& notif : m_notifications) {
        if (notif.id == id && !notif.dismissed) {
            notif.dismissed = true;
            LOG_INFO("[DbusNotificationDaemon] DbusNotification %u closed", id);
            
            // Emit DbusNotificationClosed signal
            if (m_connection && m_registrationId > 0) {
                GVariant* signal = g_variant_new("(uu)", id, 1u);  // reason=1 (dismissed)
                g_dbus_connection_emit_signal(
                    m_connection,
                    nullptr,  // destination (broadcast)
                    "/org/freedesktop/DbusNotifications",
                    "org.freedesktop.DbusNotifications",
                    "DbusNotificationClosed",
                    signal,
                    nullptr
                );
            }
            break;
        }
    }
}

void DbusNotificationDaemon::handleNotify(GDBusMethodInvocation* invocation, GVariant* params) {
    const gchar* app = "";
    const gchar* summary = "";
    const gchar* body = "";
    const gchar* icon = "";
    int timeout = 5000;
    
    // Parse parameters
    GVariant* actionsVariant = nullptr;
    GVariant* hintsVariant = nullptr;
    
    g_variant_get(params, "(&su&s&sas@a{sv}i)",
                  &app, nullptr, &icon, &summary, &body,
                  &actionsVariant, &hintsVariant, &timeout);
    
    // Extract actions
    std::vector<std::string> actions;
    if (actionsVariant) {
        gsize nActions;
        const gchar** actionsArray = g_variant_get_strv(actionsVariant, &nActions);
        for (gsize i = 0; i < nActions; i++) {
            actions.push_back(actionsArray[i]);
        }
        g_free(actionsArray);
    }
    
    // Show notification
    uint32_t id = showDbusNotification(app, summary, body, actions, timeout);
    
    // Return notification ID
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", id));
}

void DbusNotificationDaemon::handleCloseDbusNotification(GDBusMethodInvocation* invocation, GVariant* params) {
    uint32_t id;
    g_variant_get(params, "(u)", &id);
    closeDbusNotification(id);
    g_dbus_method_invocation_return_value(invocation, nullptr);
}

void DbusNotificationDaemon::handleGetCapabilities(GDBusMethodInvocation* invocation) {
    // Return list of supported capabilities
    const gchar* capabilities[] = {
        "actions",           // Supports actions
        "body",              // Supports body text
        "body-hyperlinks",   // Supports hyperlinks in body
        "body-images",       // Supports images in body
        "body-markup",       // Supports markup in body
        "icon-multi",        // Supports multiple icons
        "icon-static",       // Supports static icons
        "persistence",       // Supports persistence
        "sound",             // Supports sound
        nullptr
    };
    
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    
    for (int i = 0; capabilities[i] != nullptr; i++) {
        g_variant_builder_add(&builder, "s", capabilities[i]);
    }
    
    g_dbus_method_invocation_return_value(invocation, g_variant_builder_end(&builder));
}

void DbusNotificationDaemon::handleGetServerInformation(GDBusMethodInvocation* invocation) {
    g_dbus_method_invocation_return_value(invocation,
        g_variant_new("(ssss)",
                      getServerName().c_str(),
                      getServerVendor().c_str(),
                      getServerVersion().c_str(),
                      getSpecVersion().c_str()));
}

void DbusNotificationDaemon::onMethodCall(
    GDBusConnection* connection,
    const gchar* sender,
    const gchar* objectPath,
    const gchar* interfaceName,
    const gchar* methodName,
    GVariant* parameters,
    GDBusMethodInvocation* invocation,
    gpointer userData
) {
    DbusNotificationDaemon* daemon = static_cast<DbusNotificationDaemon*>(userData);
    
    LOG_DEBUG("[DbusNotificationDaemon] Method call: %s from %s", methodName, sender);
    
    if (g_strcmp0(methodName, "Notify") == 0) {
        daemon->handleNotify(invocation, parameters);
    } else if (g_strcmp0(methodName, "CloseDbusNotification") == 0) {
        daemon->handleCloseDbusNotification(invocation, parameters);
    } else if (g_strcmp0(methodName, "GetCapabilities") == 0) {
        daemon->handleGetCapabilities(invocation);
    } else if (g_strcmp0(methodName, "GetServerInformation") == 0) {
        daemon->handleGetServerInformation(invocation);
    } else {
        g_dbus_method_invocation_return_error(
            invocation,
            G_DBUS_ERROR,
            G_DBUS_ERROR_UNKNOWN_METHOD,
            "Unknown method: %s", methodName
        );
    }
}

} // namespace havel
