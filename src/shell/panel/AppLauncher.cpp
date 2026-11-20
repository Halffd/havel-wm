#include "AppLauncher.hpp"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

namespace havel {

AppLauncher::AppLauncher(QObject* parent)
    : QObject(parent)
{
    // Default favorites
    m_favorites = {
        "org.alacritty.Alacritty",
        "firefox.desktop",
        "org.gnome.Nautilus.desktop"
    };
}

void AppLauncher::scanApplications() {
    if (m_scanned) return;
    
    m_apps.clear();
    
    // Scan standard directories
    QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString& dir : dataDirs) {
        scanDirectory(dir);
    }
    
    m_scanned = true;
    emit scanComplete();
}

void AppLauncher::scanDirectory(const QString& dir) {
    QDir directory(dir);
    if (!directory.exists()) return;
    
    // Scan .desktop files
    for (const QString& file : directory.entryList({"*.desktop"}, QDir::Files)) {
        QString path = directory.filePath(file);
        AppEntry entry = parseDesktopFile(path);
        
        if (!entry.noDisplay && !entry.name.isEmpty()) {
            entry.id = file;
            m_apps.append(entry);
        }
    }
    
    // Scan subdirectories
    for (const QString& subDir : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        scanDirectory(directory.filePath(subDir));
    }
}

AppEntry AppLauncher::parseDesktopFile(const QString& path) {
    AppEntry entry;
    
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return entry;
    }
    
    QTextStream in(&file);
    QString content = in.readAll();
    
    entry.id = QFileInfo(path).fileName();
    entry.name = parseDesktopValue(content, "Name");
    entry.comment = parseDesktopValue(content, "Comment");
    entry.icon = parseDesktopValue(content, "Icon");
    entry.exec = parseDesktopValue(content, "Exec");
    entry.categories = parseDesktopList(content, "Categories");
    entry.noDisplay = parseDesktopValue(content, "NoDisplay").toLower() == "true";
    
    // Remove field codes from exec
    entry.exec.remove(QRegularExpression(" %[%fFuUdDnNickvm]"));
    
    return entry;
}

QString AppLauncher::parseDesktopValue(const QString& content, const QString& key) {
    QRegularExpression re(QString("^%1=(.*)$").arg(key), QRegularExpression::MultilineOption);
    QRegularExpressionMatch match = re.match(content);
    if (match.hasMatch()) {
        return match.captured(1);
    }
    return QString();
}

QStringList AppLauncher::parseDesktopList(const QString& content, const QString& key) {
    QString value = parseDesktopValue(content, key);
    if (value.isEmpty()) return {};
    return value.split(";", Qt::SkipEmptyParts);
}

QVector<AppEntry> AppLauncher::search(const QString& query) const {
    QVector<AppEntry> results;
    
    if (query.isEmpty()) {
        return results;
    }
    
    QString lowerQuery = query.toLower();
    
    for (const auto& app : m_apps) {
        bool matches = app.name.toLower().contains(lowerQuery) ||
                       app.comment.toLower().contains(lowerQuery) ||
                       app.id.toLower().contains(lowerQuery);
        
        if (matches) {
            results.append(app);
        }
        
        // Limit results
        if (results.size() >= 20) break;
    }
    
    return results;
}

QVector<AppEntry> AppLauncher::favorites() const {
    QVector<AppEntry> result;
    for (const auto& favId : m_favorites) {
        for (const auto& app : m_apps) {
            if (app.id == favId) {
                result.append(app);
                break;
            }
        }
    }
    return result;
}

void AppLauncher::setFavorites(const QStringList& appIds) {
    m_favorites = appIds;
}

bool AppLauncher::launch(const AppEntry& app) {
    if (app.exec.isEmpty()) return false;
    
    QStringList args = QProcess::splitCommand(app.exec);
    if (args.isEmpty()) return false;
    
    QString program = args.takeFirst();
    QProcess::startDetached(program, args);
    
    emit applicationLaunched(app.id);
    return true;
}

bool AppLauncher::launchById(const QString& appId) {
    for (const auto& app : m_apps) {
        if (app.id == appId) {
            return launch(app);
        }
    }
    return false;
}

void AppLauncher::launchCommand(const QString& command) {
    QProcess::startDetached(command);
}

void AppLauncher::pinApp(const QString& appId) {
    if (!m_favorites.contains(appId)) {
        m_favorites.append(appId);
    }
}

void AppLauncher::unpinApp(const QString& appId) {
    m_favorites.removeAll(appId);
}

bool AppLauncher::isPinned(const QString& appId) const {
    return m_favorites.contains(appId);
}

} // namespace havel
