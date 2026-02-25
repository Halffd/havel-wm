#include "NotificationDaemon.hpp"

#include <QTimer>

namespace havel {

NotificationDaemon::NotificationDaemon(QObject* parent)
    : QObject(parent)
{
    // Cleanup expired notifications every minute
    QTimer* cleanupTimer = new QTimer(this);
    connect(cleanupTimer, &QTimer::timeout, this, &NotificationDaemon::cleanupExpired);
    cleanupTimer->start(60000);
}

uint32_t NotificationDaemon::notify(const QString& appName, const QString& summary,
                                     const QString& body, const QString& icon,
                                     int timeout, NotificationUrgency urgency)
{
    Notification notif;
    notif.id = QString::number(generateId());
    notif.appName = appName;
    notif.summary = summary;
    notif.body = body;
    notif.icon = icon;
    notif.urgency = urgency;
    notif.timeout = timeout;
    notif.timestamp = QDateTime::currentDateTime();
    
    m_notifications.append(notif);
    
    emit notificationAdded(notif);
    emit notificationsChanged();
    
    return notif.id.toUInt();
}

void NotificationDaemon::closeNotification(uint32_t id) {
    auto it = std::find_if(m_notifications.begin(), m_notifications.end(),
        [id](const Notification& n) { return n.id.toUInt() == id; });
    
    if (it != m_notifications.end()) {
        m_notifications.erase(it);
        emit notificationClosed(id, 1);  // 1 = closed by user
        emit notificationsChanged();
    }
}

const Notification* NotificationDaemon::getNotification(uint32_t id) const {
    auto it = std::find_if(m_notifications.begin(), m_notifications.end(),
        [id](const Notification& n) { return n.id.toUInt() == id; });
    
    if (it != m_notifications.end()) {
        return &(*it);
    }
    return nullptr;
}

void NotificationDaemon::clearAll() {
    m_notifications.clear();
    emit notificationsChanged();
}

int NotificationDaemon::unreadCount() const {
    return m_notifications.size();
}

void NotificationDaemon::cleanupExpired() {
    bool changed = false;
    auto it = m_notifications.begin();
    while (it != m_notifications.end()) {
        if (it->isExpired()) {
            it = m_notifications.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    
    if (changed) {
        emit notificationsChanged();
    }
}

uint32_t NotificationDaemon::generateId() {
    return m_nextId++;
}

} // namespace havel
