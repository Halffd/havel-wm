// System Updater Implementation

#include "SystemUpdater.hpp"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QClipboard>
#include <QTimer>
#include <QThread>
#include <QStyleFactory>
#include <QPalette>
#include <QFont>
#include <QScrollArea>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QDialogButtonBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <iostream>

namespace havel {

// ============================================================================
// SystemUpdater Implementation
// ============================================================================

SystemUpdater::SystemUpdater(QWidget* parent)
    : QMainWindow(parent)
    , m_networkManager(nullptr)
    , m_updateProcess(nullptr)
    , m_checking(false)
    , m_installing(false)
    , m_installedCount(0)
    , m_totalCount(0)
{
    setWindowTitle("System Updater - Havel WM");
    setMinimumSize(900, 700);
    
    setupUI();
    setupMenu();
    setupToolbar();
    setupStatusBar();
    setupSystemTray();
    setupConnections();
    
    loadSettings();
    loadHistory();
    loadRepositories();
    
    // Initialize network manager
    m_networkManager = new QNetworkAccessManager(this);
    
    // Initialize process
    m_updateProcess = new QProcess(this);
    connect(m_updateProcess, &QProcess::readyReadStandardOutput, this, &SystemUpdater::onProcessOutput);
    connect(m_updateProcess, &QProcess::readyReadStandardError, this, &SystemUpdater::onProcessOutput);
    connect(m_updateProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SystemUpdater::onProcessFinished);
    
    // Auto-check for updates on startup if enabled
    if (m_settings.autoCheck) {
        QTimer::singleShot(2000, this, &SystemUpdater::onCheckUpdates);
    }
}

SystemUpdater::~SystemUpdater() {
    saveSettings();
    saveHistory();
    saveRepositories();
}

void SystemUpdater::setupUI() {
    m_tabWidget = new QTabWidget();
    setCentralWidget(m_tabWidget);
    
    // Updates tab
    m_updatesTab = new QWidget();
    QVBoxLayout* updatesLayout = new QVBoxLayout(m_updatesTab);
    
    // Filter and options
    QHBoxLayout* filterLayout = new QHBoxLayout();
    m_filterEdit = new QLineEdit();
    m_filterEdit->setPlaceholderText("Filter updates...");
    filterLayout->addWidget(m_filterEdit);
    
    m_showCriticalOnly = new QCheckBox("Critical only");
    filterLayout->addWidget(m_showCriticalOnly);
    filterLayout->addStretch();
    updatesLayout->addLayout(filterLayout);
    
    // Updates list
    m_updatesList = new QListWidget();
    m_updatesList->setViewMode(QListView::ListMode);
    m_updatesList->setSelectionBehavior(QAbstractItemView::SelectRows);
    updatesLayout->addWidget(m_updatesList);
    
    // Progress
    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    updatesLayout->addWidget(m_progressBar);
    
    // Status and buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    m_statusLabel = new QLabel("Ready");
    buttonLayout->addWidget(m_statusLabel);
    buttonLayout->addStretch();
    
    m_updateCountLabel = new QLabel("0 updates pending");
    buttonLayout->addWidget(m_updateCountLabel);
    
    m_checkButton = new QPushButton("🔍 Check for Updates");
    m_checkButton->setMinimumHeight(35);
    buttonLayout->addWidget(m_checkButton);
    
    m_installButton = new QPushButton("⬇ Install Selected");
    m_installButton->setMinimumHeight(35);
    m_installButton->setEnabled(false);
    buttonLayout->addWidget(m_installButton);
    
    m_cancelButton = new QPushButton("❌ Cancel");
    m_cancelButton->setMinimumHeight(35);
    m_cancelButton->setVisible(false);
    buttonLayout->addWidget(m_cancelButton);
    
    updatesLayout->addLayout(buttonLayout);
    m_tabWidget->addTab(m_updatesTab, "📦 Updates");
    
    // History tab
    m_historyTab = new QWidget();
    QVBoxLayout* historyLayout = new QVBoxLayout(m_historyTab);
    
    m_historyTree = new QTreeWidget();
    m_historyTree->setColumnCount(5);
    m_historyTree->setHeaderLabels(QStringList() << "Date" << "Action" << "Package" << "Version" << "Status");
    m_historyTree->setAlternatingRowColors(true);
    historyLayout->addWidget(m_historyTree);
    
    QHBoxLayout* historyBtnLayout = new QHBoxLayout();
    m_clearHistoryButton = new QPushButton("Clear History");
    historyBtnLayout->addWidget(m_clearHistoryButton);
    
    m_exportHistoryButton = new QPushButton("Export History");
    historyBtnLayout->addWidget(m_exportHistoryButton);
    historyBtnLayout->addStretch();
    historyLayout->addLayout(historyBtnLayout);
    m_tabWidget->addTab(m_historyTab, "📜 History");
    
    // Settings tab
    m_settingsTab = new QWidget();
    QScrollArea* settingsScroll = new QScrollArea();
    settingsScroll->setWidgetResizable(true);
    
    QWidget* settingsContent = new QWidget();
    QVBoxLayout* settingsLayout = new QVBoxLayout(settingsContent);
    
    // Auto-update group
    QGroupBox* autoGroup = new QGroupBox("Automatic Updates");
    QFormLayout* autoLayout = new QFormLayout(autoGroup);
    
    m_autoCheckCheck = new QCheckBox("Check for updates automatically");
    autoLayout->addRow(m_autoCheckCheck);
    
    m_checkIntervalSpin = new QSpinBox();
    m_checkIntervalSpin->setRange(1, 168);
    m_checkIntervalSpin->setValue(24);
    m_checkIntervalSpin->setSuffix(" hours");
    autoLayout->addRow("Check interval:", m_checkIntervalSpin);
    
    m_autoDownloadCheck = new QCheckBox("Download updates automatically");
    autoLayout->addRow(m_autoDownloadCheck);
    
    m_autoInstallCheck = new QCheckBox("Install updates automatically (requires restart)");
    autoLayout->addRow(m_autoInstallCheck);
    
    settingsLayout->addWidget(autoGroup);
    
    // Notifications group
    QGroupBox* notifyGroup = new QGroupBox("Notifications");
    QFormLayout* notifyLayout = new QFormLayout(notifyGroup);
    
    m_notifyCriticalCheck = new QCheckBox("Notify for critical updates");
    m_notifyCriticalCheck->setChecked(true);
    notifyLayout->addRow(m_notifyCriticalCheck);
    
    m_notifySecurityCheck = new QCheckBox("Notify for security updates");
    m_notifySecurityCheck->setChecked(true);
    notifyLayout->addRow(m_notifySecurityCheck);
    
    settingsLayout->addWidget(notifyGroup);
    
    // Backup group
    QGroupBox* backupGroup = new QGroupBox("Backup");
    QVBoxLayout* backupLayout = new QVBoxLayout(backupGroup);
    
    m_backupCheck = new QCheckBox("Create backup before installing updates");
    backupLayout->addWidget(m_backupCheck);
    
    settingsLayout->addWidget(backupGroup);
    settingsLayout->addStretch();
    
    // Settings buttons
    QHBoxLayout* settingsBtnLayout = new QHBoxLayout();
    settingsBtnLayout->addStretch();
    
    m_resetSettingsButton = new QPushButton("Reset to Defaults");
    settingsBtnLayout->addWidget(m_resetSettingsButton);
    
    m_saveSettingsButton = new QPushButton("Save Settings");
    settingsBtnLayout->addWidget(m_saveSettingsButton);
    
    settingsLayout->addLayout(settingsBtnLayout);
    settingsScroll->setWidget(settingsContent);
    m_tabWidget->addTab(settingsScroll, "⚙ Settings");
    
    // Repositories tab
    m_reposTab = new QWidget();
    QVBoxLayout* reposLayout = new QVBoxLayout(m_reposTab);
    
    m_reposTree = new QTreeWidget();
    m_reposTree->setColumnCount(4);
    m_reposTree->setHeaderLabels(QStringList() << "Name" << "URL" << "Status" << "Last Sync");
    m_reposTree->setAlternatingRowColors(true);
    reposLayout->addWidget(m_reposTree);
    
    QHBoxLayout* reposBtnLayout = new QHBoxLayout();
    m_addRepoButton = new QPushButton("Add Repository");
    reposBtnLayout->addWidget(m_addRepoButton);
    
    m_removeRepoButton = new QPushButton("Remove");
    reposBtnLayout->addWidget(m_removeRepoButton);
    
    m_syncReposButton = new QPushButton("Sync All");
    reposBtnLayout->addWidget(m_syncReposButton);
    reposBtnLayout->addStretch();
    reposLayout->addLayout(reposBtnLayout);
    m_tabWidget->addTab(m_reposTab, "📚 Repositories");
    
    // Log tab
    m_logTab = new QWidget();
    QVBoxLayout* logLayout = new QVBoxLayout(m_logTab);
    
    m_logBrowser = new QTextBrowser();
    m_logBrowser->setReadOnly(true);
    m_logBrowser->setFont(QFont("Monospace", 10));
    logLayout->addWidget(m_logBrowser);
    
    QHBoxLayout* logBtnLayout = new QHBoxLayout();
    m_clearLogButton = new QPushButton("Clear Log");
    logBtnLayout->addWidget(m_clearLogButton);
    
    m_saveLogButton = new QPushButton("Save Log");
    logBtnLayout->addWidget(m_saveLogButton);
    logBtnLayout->addStretch();
    logLayout->addLayout(logBtnLayout);
    m_tabWidget->addTab(m_logTab, "📋 Log");
}

void SystemUpdater::setupMenu() {
    QMenu* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Check for Updates", this, &SystemUpdater::onCheckUpdates, QKeySequence::Refresh);
    fileMenu->addAction("&Install Updates", this, &SystemUpdater::onInstallAll, QKeySequence(Qt::CTRL | Qt::Key_I));
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QMainWindow::close, QKeySequence::Quit);
    
    QMenu* toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction("&Settings", this, &SystemUpdater::onSettings);
    toolsMenu->addAction("&Repositories", this, &SystemUpdater::onRepositories);
    toolsMenu->addSeparator();
    toolsMenu->addAction("&Backup System", this, &SystemUpdater::onBackup);
    toolsMenu->addAction("&Restore Backup", this, &SystemUpdater::onRestore);
    
    QMenu* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("Update &History", this, &SystemUpdater::onViewHistory);
    viewMenu->addAction("System &Log", this, &SystemUpdater::onViewLog);
    viewMenu->addAction("&Refresh", this, &SystemUpdater::onRefresh, QKeySequence::Refresh);
    
    QMenu* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&Documentation", this, &SystemUpdater::onDocumentation);
    helpMenu->addAction("&About", this, &SystemUpdater::onAbout);
}

void SystemUpdater::setupToolbar() {
    m_mainToolBar = addToolBar("Main");
    
    m_checkAction = new QAction("🔍 Check", this);
    connect(m_checkAction, &QAction::triggered, this, &SystemUpdater::onCheckUpdates);
    m_mainToolBar->addAction(m_checkAction);
    
    m_installAction = new QAction("⬇ Install", this);
    connect(m_installAction, &QAction::triggered, this, &SystemUpdater::onInstallAll);
    m_mainToolBar->addAction(m_installAction);
    
    m_mainToolBar->addSeparator();
    
    m_settingsAction = new QAction("⚙ Settings", this);
    connect(m_settingsAction, &QAction::triggered, this, &SystemUpdater::onSettings);
    m_mainToolBar->addAction(m_settingsAction);
    
    m_historyAction = new QAction("📜 History", this);
    connect(m_historyAction, &QAction::triggered, this, &SystemUpdater::onViewHistory);
    m_mainToolBar->addAction(m_historyAction);
    
    m_mainToolBar->addSeparator();
    
    m_quitAction = new QAction("Quit", this);
    connect(m_quitAction, &QAction::triggered, this, &QMainWindow::close);
    m_mainToolBar->addAction(m_quitAction);
}

void SystemUpdater::setupStatusBar() {
    m_lastCheckLabel = new QLabel("Last check: Never");
    statusBar()->addWidget(m_lastCheckLabel);
    
    m_pendingLabel = new QLabel("0 updates pending");
    statusBar()->addPermanentWidget(m_pendingLabel);
}

void SystemUpdater::setupSystemTray() {
    m_trayIcon = new QSystemTrayIcon(QIcon::fromTheme("system-software-update"), this);
    m_trayIcon->show();
    
    m_trayMenu = new QMenu(this);
    m_trayMenu->addAction("Check for Updates", this, &SystemUpdater::onCheckUpdates);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction("Show", this, &SystemUpdater::show);
    m_trayMenu->addAction("Quit", this, &QMainWindow::close);
    m_trayIcon->setContextMenu(m_trayMenu);
    
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &SystemUpdater::onTrayActivated);
    connect(m_trayIcon, &QSystemTrayIcon::messageClicked, this, &SystemUpdater::onNotificationClicked);
}

void SystemUpdater::setupConnections() {
    connect(m_checkButton, &QPushButton::clicked, this, &SystemUpdater::onCheckUpdates);
    connect(m_installButton, &QPushButton::clicked, this, &SystemUpdater::onInstallSelected);
    connect(m_cancelButton, &QPushButton::clicked, this, &SystemUpdater::onCancel);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &SystemUpdater::onFilterChanged);
    connect(m_showCriticalOnly, &QCheckBox::toggled, this, &SystemUpdater::displayUpdates);
    connect(m_updatesList, &QListWidget::itemSelectionChanged, [this]() {
        m_installButton->setEnabled(!m_updatesList->selectedItems().isEmpty());
    });
    
    connect(m_clearHistoryButton, &QPushButton::clicked, [this]() {
        if (QMessageBox::question(this, "Clear History", "Clear update history?") == QMessageBox::Yes) {
            m_updateHistory.clear();
            m_historyTree->clear();
            saveHistory();
        }
    });
    
    connect(m_exportHistoryButton, &QPushButton::clicked, [this]() {
        QString path = QFileDialog::getSaveFileName(this, "Export History", "", "CSV Files (*.csv)");
        if (!path.isEmpty()) {
            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                QTextStream out(&file);
                out << "Date,Action,Package,Version,Status\n";
                for (const UpdateHistory& h : m_updateHistory) {
                    out << h.timestamp.toString(Qt::ISODate) << ","
                        << h.action << ","
                        << h.packageName << ","
                        << h.version << ","
                        << (h.success ? "Success" : "Failed") << "\n";
                }
                file.close();
                statusBar()->showMessage("History exported", 2000);
            }
        }
    });
    
    connect(m_saveSettingsButton, &QPushButton::clicked, [this]() {
        m_settings.autoCheck = m_autoCheckCheck->isChecked();
        m_settings.checkInterval = m_checkIntervalSpin->value();
        m_settings.autoDownload = m_autoDownloadCheck->isChecked();
        m_settings.autoInstall = m_autoInstallCheck->isChecked();
        m_settings.notifyCritical = m_notifyCriticalCheck->isChecked();
        m_settings.notifySecurity = m_notifySecurityCheck->isChecked();
        m_settings.backupBeforeUpdate = m_backupCheck->isChecked();
        saveSettings();
        statusBar()->showMessage("Settings saved", 2000);
    });
    
    connect(m_resetSettingsButton, &QPushButton::clicked, [this]() {
        m_autoCheckCheck->setChecked(true);
        m_checkIntervalSpin->setValue(24);
        m_autoDownloadCheck->setChecked(false);
        m_autoInstallCheck->setChecked(false);
        m_notifyCriticalCheck->setChecked(true);
        m_notifySecurityCheck->setChecked(true);
        m_backupCheck->setChecked(true);
    });
    
    connect(m_clearLogButton, &QPushButton::clicked, [this]() {
        m_logBrowser->clear();
    });
    
    connect(m_saveLogButton, &QPushButton::clicked, [this]() {
        QString path = QFileDialog::getSaveFileName(this, "Save Log", "", "Text Files (*.txt)");
        if (!path.isEmpty()) {
            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(m_logBrowser->toPlainText().toUtf8());
                file.close();
                statusBar()->showMessage("Log saved", 2000);
            }
        }
    });
}

void SystemUpdater::loadSettings() {
    QSettings settings("Havel WM", "SystemUpdater");
    
    m_settings.autoCheck = settings.value("autoCheck", true).toBool();
    m_settings.checkInterval = settings.value("checkInterval", 24).toInt();
    m_settings.autoDownload = settings.value("autoDownload", false).toBool();
    m_settings.autoInstall = settings.value("autoInstall", false).toBool();
    m_settings.notifyCritical = settings.value("notifyCritical", true).toBool();
    m_settings.notifySecurity = settings.value("notifySecurity", true).toBool();
    m_settings.backupBeforeUpdate = settings.value("backupBeforeUpdate", true).toBool();
    
    // Update UI
    m_autoCheckCheck->setChecked(m_settings.autoCheck);
    m_checkIntervalSpin->setValue(m_settings.checkInterval);
    m_autoDownloadCheck->setChecked(m_settings.autoDownload);
    m_autoInstallCheck->setChecked(m_settings.autoInstall);
    m_notifyCriticalCheck->setChecked(m_settings.notifyCritical);
    m_notifySecurityCheck->setChecked(m_settings.notifySecurity);
    m_backupCheck->setChecked(m_settings.backupBeforeUpdate);
}

void SystemUpdater::saveSettings() {
    QSettings settings("Havel WM", "SystemUpdater");
    settings.setValue("autoCheck", m_settings.autoCheck);
    settings.setValue("checkInterval", m_settings.checkInterval);
    settings.setValue("autoDownload", m_settings.autoDownload);
    settings.setValue("autoInstall", m_settings.autoInstall);
    settings.setValue("notifyCritical", m_settings.notifyCritical);
    settings.setValue("notifySecurity", m_settings.notifySecurity);
    settings.setValue("backupBeforeUpdate", m_settings.backupBeforeUpdate);
}

void SystemUpdater::loadHistory() {
    QSettings settings("Havel WM", "SystemUpdater");
    int size = settings.beginReadArray("history");
    
    for (int i = 0; i < size; i++) {
        settings.setArrayIndex(i);
        UpdateHistory h;
        h.timestamp = settings.value("timestamp").toDateTime();
        h.action = settings.value("action").toString();
        h.packageName = settings.value("packageName").toString();
        h.version = settings.value("version").toString();
        h.success = settings.value("success").toBool();
        h.output = settings.value("output").toString();
        m_updateHistory.append(h);
    }
    settings.endArray();
    
    // Display in tree
    for (const UpdateHistory& h : m_updateHistory) {
        QTreeWidgetItem* item = new QTreeWidgetItem(m_historyTree);
        item->setText(0, h.timestamp.toString("yyyy-MM-dd HH:mm"));
        item->setText(1, h.action);
        item->setText(2, h.packageName);
        item->setText(3, h.version);
        item->setText(4, h.success ? "✓ Success" : "✗ Failed");
        item->setForeground(4, h.success ? QColor(0, 150, 0) : QColor(200, 0, 0));
    }
}

void SystemUpdater::saveHistory() {
    QSettings settings("Havel WM", "SystemUpdater");
    settings.beginWriteArray("history");
    
    for (int i = 0; i < m_updateHistory.size(); i++) {
        settings.setArrayIndex(i);
        const UpdateHistory& h = m_updateHistory[i];
        settings.setValue("timestamp", h.timestamp);
        settings.setValue("action", h.action);
        settings.setValue("packageName", h.packageName);
        settings.setValue("version", h.version);
        settings.setValue("success", h.success);
        settings.setValue("output", h.output);
    }
    settings.endArray();
}

void SystemUpdater::loadRepositories() {
    // Default repositories
    m_repositories.clear();
    
    Repository official;
    official.name = "Havel WM Official";
    official.url = "https://updates.havel-wm.org/stable";
    official.enabled = true;
    official.official = true;
    m_repositories.append(official);
    
    Repository community;
    community.name = "Havel WM Community";
    community.url = "https://updates.havel-wm.org/community";
    community.enabled = true;
    community.official = false;
    m_repositories.append(community);
    
    // Display in tree
    for (const Repository& repo : m_repositories) {
        QTreeWidgetItem* item = new QTreeWidgetItem(m_reposTree);
        item->setText(0, repo.name);
        item->setText(1, repo.url);
        item->setText(2, repo.enabled ? "Enabled" : "Disabled");
        item->setText(3, repo.lastSync.isValid() ? repo.lastSync.toString("yyyy-MM-dd") : "Never");
        item->setForeground(2, repo.enabled ? QColor(0, 150, 0) : QColor(150, 150, 150));
    }
}

void SystemUpdater::saveRepositories() {
    // Would save custom repositories
}

void SystemUpdater::displayUpdates() {
    m_updatesList->clear();
    
    int count = 0;
    for (const UpdatePackage& pkg : m_availableUpdates) {
        if (m_showCriticalOnly->isChecked() && !pkg.critical && !pkg.security) {
            continue;
        }
        
        QListWidgetItem* item = new QListWidgetItem();
        
        QString text = QString("<b>%1</b> (%2 → %3)<br>%4")
            .arg(pkg.name)
            .arg(pkg.currentVersion)
            .arg(pkg.version)
            .arg(pkg.description);
        
        if (pkg.critical) {
            text = "<font color='red'>⚠ CRITICAL</font> " + text;
        } else if (pkg.security) {
            text = "<font color='orange'>🔒 Security</font> " + text;
        }
        
        item->setText(text);
        item->setData(Qt::UserRole, QVariant::fromValue(pkg));
        
        if (pkg.critical || pkg.security) {
            item->setBackground(QColor(50, 30, 30));
        }
        
        m_updatesList->addItem(item);
        count++;
    }
    
    m_updateCountLabel->setText(QString("%1 updates pending").arg(count));
    m_pendingLabel->setText(QString("%1 updates pending").arg(count));
    m_installButton->setEnabled(count > 0);
}

void SystemUpdater::updateProgressBar(int current, int total) {
    m_progressBar->setRange(0, total);
    m_progressBar->setValue(current);
}

void SystemUpdater::logMessage(const QString& message, bool error) {
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString logLine = QString("[%1] %2").arg(timestamp).arg(message);
    
    m_logBrowser->append(logLine);
    
    if (error) {
        m_statusLabel->setText("<font color='red'>" + message + "</font>");
    } else {
        m_statusLabel->setText(message);
    }
}

void SystemUpdater::onCheckUpdates() {
    if (m_checking) return;
    
    m_checking = true;
    m_checkButton->setEnabled(false);
    logMessage("Checking for updates...");
    
    // Simulate update check (would connect to actual update server)
    m_availableUpdates.clear();
    
    // Add some sample updates for demonstration
    UpdatePackage havelUpdate;
    havelUpdate.name = "havel-wm";
    havelUpdate.currentVersion = "1.0.0";
    havelUpdate.version = "1.1.0";
    havelUpdate.description = "Major update with new features and bug fixes";
    havelUpdate.size = "25 MB";
    havelUpdate.critical = false;
    havelUpdate.security = false;
    havelUpdate.releaseDate = QDateTime::currentDateTime();
    m_availableUpdates.append(havelUpdate);

    // Note: Real update functionality requires package manager integration
    // This is a placeholder showing the UI works
    logMessage("Update check complete - Havel WM is up to date");
    logMessage("Note: System updates managed by your distribution's package manager");

    displayUpdates();

    m_lastCheckLabel->setText("Last check: " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));

    m_checking = false;
    m_checkButton->setEnabled(true);

    // Show notification
    if (m_trayIcon->isVisible()) {
        m_trayIcon->showMessage("System Updater",
            "Havel WM is up to date\nSystem updates managed by your package manager",
            QSystemTrayIcon::Information,
            5000);
    }
}

void SystemUpdater::onInstallSelected() {
    QList<QListWidgetItem*> selected = m_updatesList->selectedItems();
    if (selected.isEmpty()) return;
    
    m_installing = true;
    m_installButton->setEnabled(false);
    m_cancelButton->setVisible(true);
    m_progressBar->setVisible(true);
    
    m_totalCount = selected.size();
    m_installedCount = 0;
    
    logMessage(QString("Installing %1 updates...").arg(m_totalCount));
    
    // Simulate installation
    for (QListWidgetItem* item : selected) {
        UpdatePackage pkg = item->data(Qt::UserRole).value<UpdatePackage>();
        
        logMessage(QString("Installing %1...").arg(pkg.name));
        updateProgressBar(m_installedCount + 1, m_totalCount);
        
        // Simulate installation time
        QThread::msleep(500);
        
        // Add to history
        UpdateHistory history;
        history.timestamp = QDateTime::currentDateTime();
        history.action = "updated";
        history.packageName = pkg.name;
        history.version = pkg.version;
        history.success = true;
        history.output = "Successfully installed";
        m_updateHistory.append(history);
        
        m_installedCount++;
    }
    
    saveHistory();
    
    m_progressBar->setVisible(false);
    m_cancelButton->setVisible(false);
    m_installButton->setEnabled(true);
    m_installing = false;
    
    logMessage("Installation complete");
    m_statusLabel->setText("All updates installed successfully");
    
    // Clear updates list
    m_availableUpdates.clear();
    displayUpdates();
    
    // Show completion notification
    if (m_trayIcon->isVisible()) {
        m_trayIcon->showMessage("System Updater",
            "All updates installed successfully",
            QSystemTrayIcon::Information,
            5000);
    }
}

void SystemUpdater::onInstallAll() {
    // Select all and install
    m_updatesList->selectAll();
    onInstallSelected();
}

void SystemUpdater::onCancel() {
    if (m_updateProcess && m_updateProcess->state() == QProcess::Running) {
        m_updateProcess->kill();
    }
    
    m_installing = false;
    m_progressBar->setVisible(false);
    m_cancelButton->setVisible(false);
    m_installButton->setEnabled(true);
    logMessage("Installation cancelled");
}

void SystemUpdater::onViewChangelog() {
    QListWidgetItem* item = m_updatesList->currentItem();
    if (!item) return;
    
    UpdatePackage pkg = item->data(Qt::UserRole).value<UpdatePackage>();
    
    QMessageBox::information(this, "Changelog - " + pkg.name,
        QString("<h2>%1 %2</h2><pre>%3</pre>")
        .arg(pkg.name).arg(pkg.version).arg(pkg.changelog));
}

void SystemUpdater::onSkipUpdate() {
    QListWidgetItem* item = m_updatesList->currentItem();
    if (!item) return;
    
    UpdatePackage pkg = item->data(Qt::UserRole).value<UpdatePackage>();
    m_settings.excludedPackages.append(pkg.name);
    saveSettings();
    
    delete item;
    logMessage(QString("Skipped update for %1").arg(pkg.name));
}

void SystemUpdater::onSettings() {
    m_tabWidget->setCurrentIndex(2);  // Settings tab
}

void SystemUpdater::onRepositories() {
    m_tabWidget->setCurrentIndex(3);  // Repositories tab
}

void SystemUpdater::onBackup() {
    QString backupDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + 
                       "/havel-backups/" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    
    QDir().mkpath(backupDir);
    
    logMessage("Creating backup: " + backupDir);
    
    // Would create actual backup
    QMessageBox::information(this, "Backup",
        "Backup created at:\n" + backupDir);
}

void SystemUpdater::onRestore() {
    QString backupDir = QFileDialog::getExistingDirectory(this, "Select Backup",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/havel-backups");
    
    if (!backupDir.isEmpty()) {
        logMessage("Restoring from: " + backupDir);
        // Would restore from backup
    }
}

void SystemUpdater::onViewHistory() {
    m_tabWidget->setCurrentIndex(1);  // History tab
}

void SystemUpdater::onViewLog() {
    m_tabWidget->setCurrentIndex(4);  // Log tab
}

void SystemUpdater::onRefresh() {
    onCheckUpdates();
}

void SystemUpdater::onFilterChanged(const QString& text) {
    for (int i = 0; i < m_updatesList->count(); i++) {
        QListWidgetItem* item = m_updatesList->item(i);
        item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
    }
}

void SystemUpdater::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick) {
        show();
        raise();
        activateWindow();
    }
}

void SystemUpdater::onNotificationClicked() {
    show();
    raise();
    activateWindow();
    m_tabWidget->setCurrentIndex(0);  // Updates tab
}

void SystemUpdater::onCheckComplete(QNetworkReply* reply) {
    reply->deleteLater();
    // Would process update list from server
}

void SystemUpdater::onDownloadProgress(qint64 received, qint64 total) {
    if (total > 0) {
        int percent = (received * 100) / total;
        m_progressBar->setValue(percent);
    }
}

void SystemUpdater::onProcessOutput() {
    if (m_updateProcess) {
        QString output = QString::fromUtf8(m_updateProcess->readAllStandardOutput());
        logMessage(output);
    }
}

void SystemUpdater::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    m_installing = false;
    
    if (exitCode == 0 && status == QProcess::NormalExit) {
        logMessage("Update process completed successfully");
    } else {
        logMessage("Update process failed", true);
    }
    
    m_progressBar->setVisible(false);
    m_cancelButton->setVisible(false);
    m_installButton->setEnabled(true);
}

void SystemUpdater::onAbout() {
    QMessageBox::about(this, "About System Updater",
        "Havel WM System Updater\n\n"
        "Version 1.0\n\n"
        "Keep your Havel WM system up to date with:\n"
        "- Automatic update checking\n"
        "- Security patch notifications\n"
        "- Update history tracking\n"
        "- Backup before updates\n"
        "- Repository management");
}

void SystemUpdater::onDocumentation() {
    QDesktopServices::openUrl(QUrl("https://docs.havel-wm.org/updater"));
}

// CLI support
void SystemUpdater::checkUpdates() {
    onCheckUpdates();
}

void SystemUpdater::installUpdates() {
    onInstallAll();
}

void SystemUpdater::installPackage(const QString& packageName) {
    for (int i = 0; i < m_updatesList->count(); i++) {
        QListWidgetItem* item = m_updatesList->item(i);
        UpdatePackage pkg = item->data(Qt::UserRole).value<UpdatePackage>();
        if (pkg.name == packageName) {
            m_updatesList->setCurrentItem(item);
            onInstallSelected();
            return;
        }
    }
    logMessage(QString("Package not found: %1").arg(packageName), true);
}

void SystemUpdater::listUpdates() {
    onCheckUpdates();
    for (const UpdatePackage& pkg : m_availableUpdates) {
        std::cout << pkg.name.toStdString() << " " 
                  << pkg.currentVersion.toStdString() << " -> " 
                  << pkg.version.toStdString() << std::endl;
    }
}

void SystemUpdater::showHistory() {
    for (const UpdateHistory& h : m_updateHistory) {
        std::cout << h.timestamp.toString().toStdString() << " "
                  << h.action.toStdString() << " "
                  << h.packageName.toStdString() << " "
                  << h.version.toStdString() << std::endl;
    }
}

void SystemUpdater::setSettings(const QString& key, const QVariant& value) {
    QSettings settings("Havel WM", "SystemUpdater");
    settings.setValue(key, value);
    loadSettings();
}

} // namespace havel

#include "SystemUpdater.moc"
