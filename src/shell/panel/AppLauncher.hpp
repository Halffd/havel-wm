#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QProcess>

namespace havel {

/**
 * Desktop application entry
 */
struct AppEntry {
    QString id;           // Desktop file ID
    QString name;         // Application name
    QString comment;      // Description/comment
    QString icon;         // Icon name
    QString exec;         // Exec command
    QStringList categories;
    bool noDisplay = false;  // Hidden from menus
};

/**
 * Application launcher - scans .desktop files and launches apps
 */
class AppLauncher : public QObject {
    Q_OBJECT

public:
    explicit AppLauncher(QObject* parent = nullptr);
    
    // Scan for applications
    void scanApplications();
    
    // Search applications
    QVector<AppEntry> search(const QString& query) const;
    
    // Get all applications
    const QVector<AppEntry>& applications() const { return m_apps; }
    
    // Get favorites (pinned apps)
    QVector<AppEntry> favorites() const;
    void setFavorites(const QStringList& appIds);
    
    // Launch application
    bool launch(const AppEntry& app);
    bool launchById(const QString& appId);
    
    // Launch by command
    static void launchCommand(const QString& command);

signals:
    void scanComplete();
    void applicationLaunched(const QString& appId);

private:
    void scanDirectory(const QString& dir);
    AppEntry parseDesktopFile(const QString& path);
    QString parseDesktopValue(const QString& content, const QString& key);
    QStringList parseDesktopList(const QString& content, const QString& key);
    
    QVector<AppEntry> m_apps;
    QStringList m_favorites;
    bool m_scanned = false;
};

} // namespace havel
