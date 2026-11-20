// File Associations Implementation

#include "FileAssociations.hpp"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QStandardPaths>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QMimeDatabase>
#include <QMimeType>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace havel {

FileAssociations::FileAssociations() : m_initialized(false) {}

FileAssociations::~FileAssociations() {}

FileAssociations& FileAssociations::instance() {
    static FileAssociations instance;
    return instance;
}

void FileAssociations::initialize() {
    if (m_initialized) return;
    
    loadFromSystem();
    m_initialized = true;
}

void FileAssociations::loadFromSystem() {
    // Scan desktop files
    scanDesktopFiles();
    
    // Load mimeapps.list
    loadMimeApps();
    
    // Add default associations for common types
    ApplicationEntry textEditor;
    textEditor.name = "Text Editor";
    textEditor.exec = "gedit %f";
    textEditor.icon = "accessories-text-editor";
    registerApplication("text/plain", textEditor);
    
    ApplicationEntry imageViewer;
    imageViewer.name = "Image Viewer";
    imageViewer.exec = "eog %f";
    imageViewer.icon = "multimedia-photo-viewer";
    registerApplication("image/jpeg", imageViewer);
    registerApplication("image/png", imageViewer);
    
    ApplicationEntry pdfViewer;
    pdfViewer.name = "PDF Viewer";
    pdfViewer.exec = "evince %f";
    pdfViewer.icon = "application-pdf";
    registerApplication("application/pdf", pdfViewer);
    
    ApplicationEntry videoPlayer;
    videoPlayer.name = "Video Player";
    videoPlayer.exec = "vlc %f";
    videoPlayer.icon = "video-x-generic";
    registerApplication("video/mp4", videoPlayer);
    registerApplication("video/webm", videoPlayer);
    
    ApplicationEntry audioPlayer;
    audioPlayer.name = "Audio Player";
    audioPlayer.exec = "rhythmbox %f";
    audioPlayer.icon = "audio-x-generic";
    registerApplication("audio/mpeg", audioPlayer);
}

void FileAssociations::scanDesktopFiles() {
    QStringList desktopPaths = QStandardPaths::locateAll(
        QStandardPaths::ApplicationsLocation, "*.desktop");
    
    for (const QString& path : desktopPaths) {
        parseDesktopFile(path);
    }
}

void FileAssociations::parseDesktopFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    
    ApplicationEntry app;
    QStringList mimeTypes;
    bool inDesktopEntry = false;
    
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        
        if (line == "[Desktop Entry]") {
            inDesktopEntry = true;
            continue;
        } else if (line.startsWith("[")) {
            inDesktopEntry = false;
            continue;
        }
        
        if (!inDesktopEntry) continue;
        
        if (line.startsWith("Name=")) {
            app.name = line.mid(5);
        } else if (line.startsWith("Exec=")) {
            app.exec = line.mid(5);
        } else if (line.startsWith("Icon=")) {
            app.icon = line.mid(5);
        } else if (line.startsWith("Comment=")) {
            app.comment = line.mid(8);
        } else if (line.startsWith("MimeType=")) {
            mimeTypes = line.mid(9).split(';');
        } else if (line.startsWith("NoDisplay=")) {
            if (line.mid(10) == "true") return;  // Skip hidden apps
        }
    }
    
    if (!app.name.isEmpty() && !app.exec.isEmpty()) {
        m_applications[app.name] = app;
        
        for (const QString& mime : mimeTypes) {
            if (!mime.isEmpty()) {
                registerApplication(mime, app);
            }
        }
    }
}

void FileAssociations::loadMimeApps() {
    QString mimeAppsPath = QStandardPaths::writableLocation(
        QStandardPaths::ConfigLocation) + "/mimeapps.list";
    
    QSettings settings(mimeAppsPath, QSettings::IniFormat);
    
    settings.beginGroup("Default Applications");
    QStringList keys = settings.allKeys();
    for (const QString& key : keys) {
        m_defaultApps[key] = settings.value(key).toString();
    }
    settings.endGroup();
}

void FileAssociations::registerApplication(const QString& mimeType, const ApplicationEntry& app) {
    if (!m_associations.contains(mimeType)) {
        m_associations[mimeType] = QList<ApplicationEntry>();
    }
    
    // Check if already registered
    for (auto& existing : m_associations[mimeType]) {
        if (existing.name == app.name) return;
    }
    
    m_associations[mimeType].append(app);
}

QList<ApplicationEntry> FileAssociations::getApplicationsForFile(const QString& filePath) const {
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(filePath);
    return getApplicationsForMimeType(mime.name());
}

QList<ApplicationEntry> FileAssociations::getApplicationsForMimeType(const QString& mimeType) const {
    QList<ApplicationEntry> result;
    
    if (m_associations.contains(mimeType)) {
        result = m_associations[mimeType];
    }
    
    // Add generic applications
    QString parentCategory = getCategoryForMimeType(mimeType);
    if (!parentCategory.isEmpty()) {
        QString parentMime = parentCategory + "/*";
        // Would expand wildcard matching
    }
    
    return result;
}

ApplicationEntry FileAssociations::getDefaultApplication(const QString& mimeType) const {
    ApplicationEntry empty;
    
    if (m_defaultApps.contains(mimeType)) {
        QString appName = m_defaultApps[mimeType];
        if (m_applications.contains(appName)) {
            return m_applications[appName];
        }
    }
    
    // Return first available application
    if (m_associations.contains(mimeType) && !m_associations[mimeType].isEmpty()) {
        return m_associations[mimeType].first();
    }
    
    return empty;
}

void FileAssociations::setDefaultApplication(const QString& mimeType, const QString& appName) {
    m_defaultApps[mimeType] = appName;
    
    // Save to mimeapps.list
    QString mimeAppsPath = QStandardPaths::writableLocation(
        QStandardPaths::ConfigLocation) + "/mimeapps.list";
    
    QSettings settings(mimeAppsPath, QSettings::IniFormat);
    settings.beginGroup("Default Applications");
    settings.setValue(mimeType, appName);
    settings.endGroup();
}

bool FileAssociations::openFile(const QString& filePath, const QString& appName) {
    if (appName.isEmpty()) {
        // Use default application
        QMimeDatabase db;
        QMimeType mime = db.mimeTypeForFile(filePath);
        ApplicationEntry app = getDefaultApplication(mime.name());
        
        if (app.name.isEmpty()) {
            // Fall back to QDesktopServices
            return QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        }
        
        return openFileWith(filePath, app.exec);
    }
    
    if (!m_applications.contains(appName)) return false;
    
    return openFileWith(filePath, m_applications[appName].exec);
}

bool FileAssociations::openFileWith(const QString& filePath, const QString& exec) {
    QString command = exec;
    command.replace("%f", filePath);
    command.replace("%F", filePath);
    command.replace("%u", filePath);
    command.replace("%U", filePath);
    
    return QProcess::startDetached(command);
}

QString FileAssociations::getMimeTypeForExtension(const QString& ext) {
    QMimeDatabase db;
    return db.mimeTypeForFile("file." + ext).name();
}

QString FileAssociations::getCategoryForMimeType(const QString& mimeType) {
    if (mimeType.startsWith("image/")) return "image";
    if (mimeType.startsWith("video/")) return "video";
    if (mimeType.startsWith("audio/")) return "audio";
    if (mimeType.startsWith("text/")) return "text";
    if (mimeType.startsWith("application/")) return "application";
    return "";
}

} // namespace havel
