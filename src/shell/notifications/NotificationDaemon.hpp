#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QDateTime>
#include <QUuid>

namespace havel {

/**
 * Notification urgency levels
 */
enum class NotificationUrgency {
    Low,
    Normal,
    Critical
};

/**
 * Single notification
 */
struct Notification {
    QString id;
    QString appName;
    QString summary;
    QString body;
    QString icon;
    NotificationUrgency urgency = NotificationUrgency::Normal;
    int timeout = 5000;  // ms, -1 = never
    QDateTime timestamp;
    QStringList actions;  // Action keys
    QVariantMap hints;
    
    bool isExpired() const {
        if (timeout < 0) return false;
        return timestamp.msecsTo(QDateTime::currentDateTime()) > timeout;
    }
};

/**
 * Notifications daemon - implements freedesktop.org notification spec
 */
class NotificationDaemon : public QObject {
    Q_OBJECT

public:
    explicit NotificationDaemon(QObject* parent = nullptr);
    
    // Show notification
    uint32_t notify(const QString& appName, const QString& summary,
                    const QString& body = QString(),
                    const QString& icon = QString(),
                    int timeout = 5000,
                    NotificationUrgency urgency = NotificationUrgency::Normal);
    
    // Close notification
    void closeNotification(uint32_t id);
    
    // Get all active notifications
    QVector<Notification> notifications() const { return m_notifications; }
    
    // Get notification by ID
    const Notification* getNotification(uint32_t id) const;
    
    // Clear all notifications
    void clearAll();
    
    // Get unread count
    int unreadCount() const;

signals:
    void notificationAdded(const Notification& notif);
    void notificationClosed(uint32_t id, uint32_t reason);
    void actionInvoked(uint32_t id, const QString& actionKey);
    void notificationsChanged();

private:
    void cleanupExpired();
    uint32_t generateId();
    
    QVector<Notification> m_notifications;
    uint32_t m_nextId = 1;
};

} // namespace havel
