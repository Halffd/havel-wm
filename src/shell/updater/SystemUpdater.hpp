// System Updater for Havel WM

#pragma once

#include <QMainWindow>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTextBrowser>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QToolBar>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QSystemTrayIcon>
#include <QSettings>
#include <QProcess>
#include <QTimer>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStandardPaths>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <QSplitter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QProgressDialog>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QIcon>
#include <QPainter>
#include <QStyleFactory>
#include <QPalette>
#include <QFont>
#include <QSpinBox>
#include <QTimeEdit>
#include <QCalendarWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStackedWidget>
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>

namespace havel {

/**
 * Update package structure
 */
struct UpdatePackage {
    QString name;
    QString version;
    QString currentVersion;
    QString description;
    QString size;
    bool critical;
    bool security;
    QStringList dependencies;
    QDateTime releaseDate;
    QString changelog;
    
    UpdatePackage() : critical(false), security(false) {}
};

/**
 * Update history entry
 */
struct UpdateHistory {
    QDateTime timestamp;
    QString action;  // installed, removed, updated
    QString packageName;
    QString version;
    bool success;
    QString output;
};

/**
 * Repository information
 */
struct Repository {
    QString name;
    QString url;
    bool enabled;
    bool official;
    QDateTime lastSync;
};

/**
 * Update settings
 */
struct UpdateSettings {
    bool autoCheck;
    int checkInterval;  // hours
    bool autoDownload;
    bool autoInstall;
    bool notifyCritical;
    bool notifySecurity;
    bool notifyAll;
    QString scheduleTime;
    QStringList excludedPackages;
    bool backupBeforeUpdate;
};

/**
 * Main System Updater Window
 */
class SystemUpdater : public QMainWindow {
    Q_OBJECT

public:
    explicit SystemUpdater(QWidget* parent = nullptr);
    ~SystemUpdater();
    
    // CLI support
    void checkUpdates();
    void installUpdates();
    void installPackage(const QString& packageName);
    void listUpdates();
    void showHistory();
    void setSettings(const QString& key, const QVariant& value);
    
private slots:
    // Update operations
    void onCheckUpdates();
    void onInstallSelected();
    void onInstallAll();
    void onCancel();
    void onViewChangelog();
    void onSkipUpdate();
    
    // Settings
    void onSettings();
    void onRepositories();
    void onBackup();
    void onRestore();
    
    // View operations
    void onViewHistory();
    void onViewLog();
    void onRefresh();
    void onFilterChanged(const QString& text);
    
    // System tray
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onNotificationClicked();
    
    // Network
    void onCheckComplete(QNetworkReply* reply);
    void onDownloadProgress(qint64 received, qint64 total);
    
    // Process
    void onProcessOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    
    // Help
    void onAbout();
    void onDocumentation();

private:
    void setupUI();
    void setupMenu();
    void setupToolbar();
    void setupStatusBar();
    void setupSystemTray();
    void setupConnections();
    
    void loadSettings();
    void saveSettings();
    void loadHistory();
    void saveHistory();
    void loadRepositories();
    void saveRepositories();
    
    void displayUpdates();
    void updateProgressBar(int current, int total);
    void logMessage(const QString& message, bool error = false);
    
    // UI components
    QTabWidget* m_tabWidget;
    
    // Updates tab
    QWidget* m_updatesTab;
    QListWidget* m_updatesList;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QPushButton* m_checkButton;
    QPushButton* m_installButton;
    QPushButton* m_cancelButton;
    QLineEdit* m_filterEdit;
    QCheckBox* m_showCriticalOnly;
    QLabel* m_updateCountLabel;
    
    // History tab
    QWidget* m_historyTab;
    QTreeWidget* m_historyTree;
    QPushButton* m_clearHistoryButton;
    QPushButton* m_exportHistoryButton;
    
    // Settings tab
    QWidget* m_settingsTab;
    QCheckBox* m_autoCheckCheck;
    QSpinBox* m_checkIntervalSpin;
    QCheckBox* m_autoDownloadCheck;
    QCheckBox* m_autoInstallCheck;
    QCheckBox* m_notifyCriticalCheck;
    QCheckBox* m_notifySecurityCheck;
    QCheckBox* m_backupCheck;
    QPushButton* m_saveSettingsButton;
    QPushButton* m_resetSettingsButton;
    
    // Repositories tab
    QWidget* m_reposTab;
    QTreeWidget* m_reposTree;
    QPushButton* m_addRepoButton;
    QPushButton* m_removeRepoButton;
    QPushButton* m_syncReposButton;
    
    // Log tab
    QWidget* m_logTab;
    QTextBrowser* m_logBrowser;
    QPushButton* m_clearLogButton;
    QPushButton* m_saveLogButton;
    
    // Toolbar
    QToolBar* m_mainToolBar;
    QAction* m_checkAction;
    QAction* m_installAction;
    QAction* m_settingsAction;
    QAction* m_historyAction;
    QAction* m_quitAction;
    
    // Status bar
    QLabel* m_lastCheckLabel;
    QLabel* m_pendingLabel;
    
    // System tray
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_trayMenu;
    
    // Network
    QNetworkAccessManager* m_networkManager;
    
    // Process
    QProcess* m_updateProcess;
    
    // Data
    QList<UpdatePackage> m_availableUpdates;
    QList<UpdateHistory> m_updateHistory;
    QList<Repository> m_repositories;
    UpdateSettings m_settings;
    
    // State
    bool m_checking;
    bool m_installing;
    int m_installedCount;
    int m_totalCount;
};

/**
 * Settings dialog
 */
class UpdateSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit UpdateSettingsDialog(const UpdateSettings& settings, QWidget* parent = nullptr);
    UpdateSettings getSettings() const { return m_settings; }
    
private:
    void setupUI();
    
    UpdateSettings m_settings;
    
    QCheckBox* m_autoCheck;
    QSpinBox* m_checkInterval;
    QCheckBox* m_autoDownload;
    QCheckBox* m_autoInstall;
    QCheckBox* m_notifyCritical;
    QCheckBox* m_notifySecurity;
    QCheckBox* m_backup;
    QTimeEdit* m_scheduleTime;
};

} // namespace havel
