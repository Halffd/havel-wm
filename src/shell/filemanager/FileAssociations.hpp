// File Associations - Map file types to applications

#pragma once

#include <QString>
#include <QMap>
#include <QList>
#include <QIcon>

namespace havel {

/**
 * Application entry for file associations
 */
struct ApplicationEntry {
    QString name;
    QString exec;      // Command to execute (%f for file)
    QString icon;
    QString comment;
    bool supportsMultiple = false;
    bool isDefault = false;
};

/**
 * File Association Manager
 */
class FileAssociations {
public:
    static FileAssociations& instance();
    
    // Initialize
    void initialize();
    void loadFromSystem();
    
    // Get applications for file
    QList<ApplicationEntry> getApplicationsForFile(const QString& filePath) const;
    QList<ApplicationEntry> getApplicationsForMimeType(const QString& mimeType) const;
    
    // Get default application
    ApplicationEntry getDefaultApplication(const QString& mimeType) const;
    void setDefaultApplication(const QString& mimeType, const QString& appName);
    
    // Open file with application
    bool openFile(const QString& filePath, const QString& appName);
    bool openFileWith(const QString& filePath, const QString& exec);
    
    // Register application
    void registerApplication(const QString& mimeType, const ApplicationEntry& app);
    
    // Scan desktop files
    void scanDesktopFiles();
    
    // Common MIME types
    static QString getMimeTypeForExtension(const QString& ext);
    static QString getCategoryForMimeType(const QString& mimeType);

private:
    FileAssociations();
    ~FileAssociations();
    
    void parseDesktopFile(const QString& path);
    void loadMimeApps();
    
    // MIME type -> Applications
    QMap<QString, QList<ApplicationEntry>> m_associations;
    QMap<QString, QString> m_defaultApps;
    
    // Application name -> Entry
    QMap<QString, ApplicationEntry> m_applications;
    
    bool m_initialized;
};

} // namespace havel
