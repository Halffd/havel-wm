// File Properties Dialog Implementation

#include "FileProperties.hpp"
#include "FileAssociations.hpp"
#include "ThumbnailGenerator.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QStandardPaths>
#include <QIcon>
#include <QPainter>
#include <QSystemTrayIcon>
#include <QMimeDatabase>
#include <QMimeType>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

namespace havel {

// ============================================================================
// FilePropertiesDialog Implementation
// ============================================================================

FilePropertiesDialog::FilePropertiesDialog(const QString& filePath, QWidget* parent)
    : QDialog(parent)
    , m_filePath(filePath)
    , m_fileInfo(filePath)
    , m_tabWidget(nullptr)
    , m_iconLabel(nullptr)
    , m_nameEdit(nullptr)
    , m_typeLabel(nullptr)
    , m_sizeLabel(nullptr)
    , m_modifiedLabel(nullptr)
    , m_accessedLabel(nullptr)
    , m_createdLabel(nullptr)
    , m_pathLabel(nullptr)
    , m_appList(nullptr)
    , m_chooseAppButton(nullptr)
    , m_ownerRead(nullptr)
    , m_ownerWrite(nullptr)
    , m_ownerExec(nullptr)
    , m_groupRead(nullptr)
    , m_groupWrite(nullptr)
    , m_groupExec(nullptr)
    , m_otherRead(nullptr)
    , m_otherWrite(nullptr)
    , m_otherExec(nullptr)
    , m_setuid(nullptr)
    , m_setgid(nullptr)
    , m_okButton(nullptr)
    , m_cancelButton(nullptr)
    , m_applyButton(nullptr)
{
    setWindowTitle("Properties - " + m_fileInfo.fileName());
    setMinimumSize(500, 400);
    
    m_tabWidget = new QTabWidget(this);
    
    setupBasicTab();
    setupOpenWithTab();
    setupPermissionsTab();
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(m_tabWidget);
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_applyButton = new QPushButton("Apply");
    m_cancelButton = new QPushButton("Cancel");
    m_okButton = new QPushButton("OK");
    
    connect(m_applyButton, &QPushButton::clicked, this, &FilePropertiesDialog::applyChanges);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_okButton, &QPushButton::clicked, [this]() {
        applyChanges();
        accept();
    });
    
    buttonLayout->addWidget(m_applyButton);
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_okButton);
    
    mainLayout->addLayout(buttonLayout);
}

void FilePropertiesDialog::setupBasicTab() {
    QWidget* basicTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(basicTab);
    
    // File icon and name
    QHBoxLayout* iconLayout = new QHBoxLayout();
    
    m_iconLabel = new QLabel();
    m_iconLabel->setFixedSize(64, 64);
    
    // Generate thumbnail or use default icon
    QImage thumbnail = ThumbnailGenerator::instance().generateThumbnail(
        m_filePath, ThumbnailSize::Large);
    
    if (!thumbnail.isNull()) {
        m_iconLabel->setPixmap(QPixmap::fromImage(thumbnail));
    } else {
        // Create default icon
        QImage icon(64, 64, QImage::Format_ARGB32);
        icon.fill(Qt::transparent);
        QPainter painter(&icon);
        painter.setBrush(m_fileInfo.isDir() ? QColor(200, 150, 50) : QColor(150, 150, 200));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(4, 4, 56, 56, 8, 8);
        m_iconLabel->setPixmap(QPixmap::fromImage(icon));
    }
    
    iconLayout->addWidget(m_iconLabel);
    
    QVBoxLayout* nameLayout = new QVBoxLayout();
    m_nameEdit = new QLineEdit(m_fileInfo.fileName());
    m_nameEdit->setReadOnly(m_fileInfo.isDir());  // Can't rename directories easily
    
    QLabel* typeLabel = new QLabel("Type:");
    m_typeLabel = new QLabel(m_fileInfo.isDir() ? "Folder" : 
        QMimeDatabase().mimeTypeForFile(m_filePath).comment());
    
    nameLayout->addWidget(new QLabel("Name:"));
    nameLayout->addWidget(m_nameEdit);
    nameLayout->addWidget(typeLabel);
    nameLayout->addWidget(m_typeLabel);
    
    iconLayout->addLayout(nameLayout);
    iconLayout->addStretch();
    layout->addLayout(iconLayout);
    
    // File info group
    QGroupBox* infoGroup = new QGroupBox("Information");
    QFormLayout* formLayout = new QFormLayout(infoGroup);
    
    m_sizeLabel = new QLabel(formatFileSize(m_fileInfo.size()));
    formLayout->addRow("Size:", m_sizeLabel);
    
    m_modifiedLabel = new QLabel(formatDateTime(m_fileInfo.lastModified()));
    formLayout->addRow("Modified:", m_modifiedLabel);
    
    m_accessedLabel = new QLabel(formatDateTime(m_fileInfo.lastRead()));
    formLayout->addRow("Accessed:", m_accessedLabel);
    
    m_createdLabel = new QLabel(formatDateTime(m_fileInfo.birthTime()));
    formLayout->addRow("Created:", m_createdLabel);
    
    m_pathLabel = new QLabel(m_fileInfo.canonicalFilePath());
    m_pathLabel->setWordWrap(true);
    formLayout->addRow("Location:", m_pathLabel);
    
    layout->addWidget(infoGroup);
    layout->addStretch();
    
    m_tabWidget->addTab(basicTab, "Basic");
    
    connect(m_nameEdit, &QLineEdit::textChanged, this, &FilePropertiesDialog::onNameChanged);
}

void FilePropertiesDialog::setupOpenWithTab() {
    QWidget* openWithTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(openWithTab);
    
    layout->addWidget(new QLabel("Select application to open this file:"));
    
    m_appList = new QListWidget();
    m_appList->setViewMode(QListWidget::ListMode);
    
    // Get applications for this file type
    QList<ApplicationEntry> apps = FileAssociations::instance()
        .getApplicationsForFile(m_filePath);
    
    ApplicationEntry defaultApp = FileAssociations::instance()
        .getDefaultApplication(QMimeDatabase().mimeTypeForFile(m_filePath).name());
    
    for (const auto& app : apps) {
        QListWidgetItem* item = new QListWidgetItem(app.name);
        if (!app.icon.isEmpty()) {
            item->setIcon(QIcon::fromTheme(app.icon));
        }
        if (app.name == defaultApp.name) {
            item->setText(item->text() + " (Default)");
            item->setSelected(true);
        }
        m_appList->addItem(item);
    }
    
    layout->addWidget(m_appList);
    
    m_chooseAppButton = new QPushButton("Choose Custom Application...");
    layout->addWidget(m_chooseAppButton);
    
    layout->addStretch();
    
    m_tabWidget->addTab(openWithTab, "Open With");
    
    connect(m_appList, &QListWidget::currentRowChanged, this, &FilePropertiesDialog::onOpenWithSelected);
    connect(m_chooseAppButton, &QPushButton::clicked, this, &FilePropertiesDialog::onChooseAppClicked);
}

void FilePropertiesDialog::setupPermissionsTab() {
    QWidget* permsTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(permsTab);
    
    // Get current permissions
    struct stat st;
    if (stat(m_filePath.toLocal8Bit().constData(), &st) != 0) {
        layout->addWidget(new QLabel("Could not read file permissions"));
        m_tabWidget->addTab(permsTab, "Permissions");
        return;
    }
    
    // Owner permissions
    QGroupBox* ownerGroup = new QGroupBox("Owner");
    QHBoxLayout* ownerLayout = new QHBoxLayout(ownerGroup);
    
    m_ownerRead = new QCheckBox("Read");
    m_ownerWrite = new QCheckBox("Write");
    m_ownerExec = new QCheckBox("Execute");
    
    m_ownerRead->setChecked(st.st_mode & S_IRUSR);
    m_ownerWrite->setChecked(st.st_mode & S_IWUSR);
    m_ownerExec->setChecked(st.st_mode & S_IXUSR);
    
    ownerLayout->addWidget(m_ownerRead);
    ownerLayout->addWidget(m_ownerWrite);
    ownerLayout->addWidget(m_ownerExec);
    
    layout->addWidget(ownerGroup);
    
    // Group permissions
    QGroupBox* groupGroup = new QGroupBox("Group");
    QHBoxLayout* groupLayout = new QHBoxLayout(groupGroup);
    
    m_groupRead = new QCheckBox("Read");
    m_groupWrite = new QCheckBox("Write");
    m_groupExec = new QCheckBox("Execute");
    
    m_groupRead->setChecked(st.st_mode & S_IRGRP);
    m_groupWrite->setChecked(st.st_mode & S_IWGRP);
    m_groupExec->setChecked(st.st_mode & S_IXGRP);
    
    groupLayout->addWidget(m_groupRead);
    groupLayout->addWidget(m_groupWrite);
    groupLayout->addWidget(m_groupExec);
    
    layout->addWidget(groupGroup);
    
    // Other permissions
    QGroupBox* otherGroup = new QGroupBox("Others");
    QHBoxLayout* otherLayout = new QHBoxLayout(otherGroup);
    
    m_otherRead = new QCheckBox("Read");
    m_otherWrite = new QCheckBox("Write");
    m_otherExec = new QCheckBox("Execute");
    
    m_otherRead->setChecked(st.st_mode & S_IROTH);
    m_otherWrite->setChecked(st.st_mode & S_IWOTH);
    m_otherExec->setChecked(st.st_mode & S_IXOTH);
    
    otherLayout->addWidget(m_otherRead);
    otherLayout->addWidget(m_otherWrite);
    otherLayout->addWidget(m_otherExec);
    
    layout->addWidget(otherGroup);
    
    // Special bits
    QGroupBox* specialGroup = new QGroupBox("Special");
    QHBoxLayout* specialLayout = new QHBoxLayout(specialGroup);
    
    m_setuid = new QCheckBox("Set UID");
    m_setgid = new QCheckBox("Set GID");
    
    m_setuid->setChecked(st.st_mode & S_ISUID);
    m_setgid->setChecked(st.st_mode & S_ISGID);
    
    specialLayout->addWidget(m_setuid);
    specialLayout->addWidget(m_setgid);
    
    layout->addWidget(specialGroup);
    layout->addStretch();
    
    m_tabWidget->addTab(permsTab, "Permissions");
    
    connect(m_ownerRead, &QCheckBox::toggled, this, &FilePropertiesDialog::onPermissionsChanged);
    connect(m_ownerWrite, &QCheckBox::toggled, this, &FilePropertiesDialog::onPermissionsChanged);
    connect(m_ownerExec, &QCheckBox::toggled, this, &FilePropertiesDialog::onPermissionsChanged);
    connect(m_groupRead, &QCheckBox::toggled, this, &FilePropertiesDialog::onPermissionsChanged);
    connect(m_groupWrite, &QCheckBox::toggled, this, &FilePropertiesDialog::onPermissionsChanged);
    connect(m_groupExec, &QCheckBox::toggled, this, &FilePropertiesDialog::onPermissionsChanged);
    connect(m_otherRead, &QCheckBox::toggled, this, &FilePropertiesDialog::onPermissionsChanged);
    connect(m_otherWrite, &QCheckBox::toggled, this, &FilePropertiesDialog::onPermissionsChanged);
    connect(m_otherExec, &QCheckBox::toggled, this, &FilePropertiesDialog::onPermissionsChanged);
}

void FilePropertiesDialog::onNameChanged(const QString& text) {
    // Live preview of name change
}

void FilePropertiesDialog::onOpenWithSelected(int index) {
    // Update default application selection
}

void FilePropertiesDialog::onChooseAppClicked() {
    QString appPath = QFileDialog::getOpenFileName(this, "Choose Application",
        "/usr/bin", "Applications (*.desktop);;All Files (*)");
    
    if (!appPath.isEmpty()) {
        // Would add custom application to list
    }
}

void FilePropertiesDialog::onPermissionsChanged() {
    // Permissions changed, enable Apply button
}

void FilePropertiesDialog::applyChanges() {
    // Rename file if name changed
    if (m_nameEdit->text() != m_fileInfo.fileName() && !m_fileInfo.isDir()) {
        QString newPath = m_fileInfo.path() + "/" + m_nameEdit->text();
        if (!QFile::rename(m_filePath, newPath)) {
            QMessageBox::critical(this, "Error", "Failed to rename file");
            return;
        }
        m_filePath = newPath;
        m_fileInfo = QFileInfo(newPath);
    }
    
    // Update permissions
    struct stat st;
    if (stat(m_filePath.toLocal8Bit().constData(), &st) == 0) {
        mode_t mode = st.st_mode;
        
        // Clear old permissions
        mode &= ~(S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID | S_ISGID);
        
        // Set new permissions
        if (m_ownerRead->isChecked()) mode |= S_IRUSR;
        if (m_ownerWrite->isChecked()) mode |= S_IWUSR;
        if (m_ownerExec->isChecked()) mode |= S_IXUSR;
        if (m_groupRead->isChecked()) mode |= S_IRGRP;
        if (m_groupWrite->isChecked()) mode |= S_IWGRP;
        if (m_groupExec->isChecked()) mode |= S_IXGRP;
        if (m_otherRead->isChecked()) mode |= S_IROTH;
        if (m_otherWrite->isChecked()) mode |= S_IWOTH;
        if (m_otherExec->isChecked()) mode |= S_IXOTH;
        if (m_setuid->isChecked()) mode |= S_ISUID;
        if (m_setgid->isChecked()) mode |= S_ISGID;
        
        chmod(m_filePath.toLocal8Bit().constData(), mode);
    }
    
    // Update default application if changed
    if (m_appList && m_appList->currentItem()) {
        QString appName = m_appList->currentItem()->text();
        appName.remove(" (Default)");
        
        QString mimeType = QMimeDatabase().mimeTypeForFile(m_filePath).name();
        FileAssociations::instance().setDefaultApplication(mimeType, appName);
    }
    
    // Changes applied successfully
    m_fileInfo = QFileInfo(m_filePath);  // Refresh file info
}

QString FilePropertiesDialog::formatFileSize(qint64 size) {
    if (size < 1024) return QString::number(size) + " B";
    if (size < 1024 * 1024) return QString::number(size / 1024.0, 'f', 1) + " KB";
    if (size < 1024 * 1024 * 1024) return QString::number(size / (1024.0 * 1024.0), 'f', 1) + " MB";
    return QString::number(size / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GB";
}

QString FilePropertiesDialog::formatDateTime(const QDateTime& dt) {
    if (!dt.isValid()) return "Unknown";
    return dt.toString("yyyy-MM-dd HH:mm:ss");
}

// ============================================================================
// FilePickerDialog Implementation
// ============================================================================

FilePickerDialog::FilePickerDialog(QWidget* parent, Mode mode, const QString& startPath)
    : QDialog(parent)
    , m_mode(mode)
    , m_currentPath(startPath.isEmpty() ? QDir::homePath() : startPath)
    , m_startPath(startPath)
    , m_pathBar(nullptr)
    , m_filterBar(nullptr)
    , m_nameEdit(nullptr)
    , m_fileList(nullptr)
    , m_okButton(nullptr)
    , m_cancelButton(nullptr)
    , m_upButton(nullptr)
    , m_homeButton(nullptr)
    , m_refreshButton(nullptr)
    , m_newFolderButton(nullptr)
    , m_statusLabel(nullptr)
{
    setWindowTitle(mode == Mode::OpenFile ? "Open File" :
                   mode == Mode::OpenDirectory ? "Open Directory" :
                   mode == Mode::SaveFile ? "Save File" : "Select Files");
    setMinimumSize(700, 500);
    
    setupUI();
    updateFileList();
    updatePathBar();
}

void FilePickerDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Path bar
    QHBoxLayout* pathLayout = new QHBoxLayout();
    m_upButton = new QPushButton("⬆");
    m_upButton->setMaximumWidth(40);
    m_homeButton = new QPushButton("🏠");
    m_homeButton->setMaximumWidth(40);
    m_refreshButton = new QPushButton("🔄");
    m_refreshButton->setMaximumWidth(40);
    
    m_pathBar = new QLineEdit(m_currentPath);
    m_pathBar->setReadOnly(true);
    
    pathLayout->addWidget(m_upButton);
    pathLayout->addWidget(m_homeButton);
    pathLayout->addWidget(m_pathBar);
    pathLayout->addWidget(m_refreshButton);
    
    mainLayout->addLayout(pathLayout);
    
    // Filter bar
    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->addWidget(new QLabel("Filter:"));
    m_filterBar = new QLineEdit();
    m_filterBar->setPlaceholderText("Type to filter files...");
    m_filterBar->setClearButtonEnabled(true);
    filterLayout->addWidget(m_filterBar);
    
    mainLayout->addLayout(filterLayout);
    
    // File list
    m_fileList = new QListWidget();
    m_fileList->setViewMode(QListWidget::ListMode);
    m_fileList->setSortingEnabled(true);
    
    if (m_mode == Mode::MultipleFiles) {
        m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    }
    
    mainLayout->addWidget(m_fileList);
    
    // Name edit (for save dialog)
    if (m_mode == Mode::SaveFile) {
        QHBoxLayout* nameLayout = new QHBoxLayout();
        nameLayout->addWidget(new QLabel("File name:"));
        m_nameEdit = new QLineEdit();
        nameLayout->addWidget(m_nameEdit);
        mainLayout->addLayout(nameLayout);
    }
    
    // Status bar
    m_statusLabel = new QLabel();
    mainLayout->addWidget(m_statusLabel);
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_newFolderButton = new QPushButton("New Folder");
    buttonLayout->addWidget(m_newFolderButton);
    buttonLayout->addStretch();
    
    m_cancelButton = new QPushButton("Cancel");
    m_okButton = new QPushButton(m_mode == Mode::SaveFile ? "Save" : "Open");
    m_okButton->setDefault(true);
    
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_okButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Connections
    connect(m_upButton, &QPushButton::clicked, this, &FilePickerDialog::onUpClicked);
    connect(m_homeButton, &QPushButton::clicked, this, &FilePickerDialog::onHomeClicked);
    connect(m_refreshButton, &QPushButton::clicked, this, &FilePickerDialog::onRefreshClicked);
    connect(m_newFolderButton, &QPushButton::clicked, this, &FilePickerDialog::onNewFolderClicked);
    connect(m_fileList, &QListWidget::itemDoubleClicked, [this](QListWidgetItem* item) {
        onFileDoubleClicked(m_fileList->indexFromItem(item));
    });
    connect(m_fileList, &QListWidget::itemSelectionChanged, this, &FilePickerDialog::onSelectionChanged);
    connect(m_filterBar, &QLineEdit::textChanged, this, &FilePickerDialog::onFilterChanged);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_okButton, &QPushButton::clicked, this, &FilePickerDialog::onOkClicked);
}

void FilePickerDialog::updateFileList() {
    m_fileList->clear();
    
    QDir dir(m_currentPath);
    QFileInfoList files = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, 
                                             QDir::DirsFirst | QDir::Name);
    
    int fileCount = 0;
    int dirCount = 0;
    
    for (const QFileInfo& fi : files) {
        if (!acceptFile(fi)) continue;
        
        QListWidgetItem* item = new QListWidgetItem();
        item->setText(fi.fileName());
        
        if (fi.isDir()) {
            item->setIcon(QIcon::fromTheme("folder", QIcon::fromTheme("inode/directory")));
            dirCount++;
        } else {
            item->setIcon(QIcon::fromTheme("text-x-generic"));
            fileCount++;
        }
        
        // Add file size for files
        if (!fi.isDir()) {
            QString sizeStr;
            qint64 size = fi.size();
            if (size < 1024) sizeStr = QString::number(size) + " B";
            else if (size < 1024 * 1024) sizeStr = QString::number(size / 1024.0, 'f', 1) + " KB";
            else if (size < 1024 * 1024 * 1024) sizeStr = QString::number(size / (1024.0 * 1024.0), 'f', 1) + " MB";
            else sizeStr = QString::number(size / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GB";
            item->setText(item->text() + QString(" (%1)").arg(sizeStr));
        }
        
        m_fileList->addItem(item);
    }
    
    m_statusLabel->setText(QString("%1 folders, %2 files").arg(dirCount).arg(fileCount));
}

void FilePickerDialog::updatePathBar() {
    m_pathBar->setText(m_currentPath);
}

bool FilePickerDialog::acceptFile(const QFileInfo& fi) const {
    // Apply name filters
    if (!m_nameFilters.isEmpty() && !fi.isDir()) {
        bool matches = false;
        for (QString filter : m_nameFilters) {
            filter.remove(QChar('*'));
            if (fi.fileName().endsWith(filter, Qt::CaseInsensitive)) {
                matches = true;
                break;
            }
        }
        if (!matches) return false;
    }
    
    // Apply filter bar text
    if (!m_filterBar->text().isEmpty()) {
        if (!fi.fileName().contains(m_filterBar->text(), Qt::CaseInsensitive)) {
            return false;
        }
    }
    
    return true;
}

void FilePickerDialog::onDirectoryClicked(const QModelIndex& index) {
    // Update selection
}

void FilePickerDialog::onFileDoubleClicked(const QModelIndex& index) {
    QString path = m_currentPath + "/" + m_fileList->item(index.row())->text().split(" (").first();
    QFileInfo fi(path);
    
    if (fi.isDir()) {
        m_currentPath = path;
        updateFileList();
        updatePathBar();
    } else {
        if (m_mode != Mode::OpenDirectory) {
            m_selectedFile = path;
            accept();
        }
    }
}

void FilePickerDialog::onSelectionChanged() {
    QList<QListWidgetItem*> selected = m_fileList->selectedItems();
    
    if (selected.size() == 1) {
        QString name = selected.first()->text().split(" (").first();
        if (m_mode == Mode::SaveFile && m_nameEdit) {
            m_nameEdit->setText(name);
        }
    }
}

void FilePickerDialog::onOkClicked() {
    if (m_mode == Mode::MultipleFiles) {
        QList<QListWidgetItem*> selected = m_fileList->selectedItems();
        for (QListWidgetItem* item : selected) {
            QString name = item->text().split(" (").first();
            m_selectedFiles.append(m_currentPath + "/" + name);
        }
        
        if (m_selectedFiles.isEmpty()) {
            reject();
        } else {
            accept();
        }
    } else {
        QList<QListWidgetItem*> selected = m_fileList->selectedItems();
        
        if (selected.isEmpty()) {
            if (m_mode == Mode::SaveFile && m_nameEdit && !m_nameEdit->text().isEmpty()) {
                m_selectedFile = m_currentPath + "/" + m_nameEdit->text();
                accept();
            } else {
                reject();
            }
        } else {
            QString name = selected.first()->text().split(" (").first();
            m_selectedFile = m_currentPath + "/" + name;
            accept();
        }
    }
}

void FilePickerDialog::onCancelClicked() {
    reject();
}

void FilePickerDialog::onUpClicked() {
    QDir dir(m_currentPath);
    if (dir.cdUp()) {
        m_currentPath = dir.path();
        updateFileList();
        updatePathBar();
    }
}

void FilePickerDialog::onHomeClicked() {
    m_currentPath = QDir::homePath();
    updateFileList();
    updatePathBar();
}

void FilePickerDialog::onRefreshClicked() {
    updateFileList();
}

void FilePickerDialog::onNewFolderClicked() {
    bool ok;
    QString name = QInputDialog::getText(this, "New Folder", "Folder name:");
    
    if (ok && !name.isEmpty()) {
        QDir(m_currentPath).mkdir(name);
        updateFileList();
    }
}

void FilePickerDialog::onFilterChanged(const QString& text) {
    updateFileList();
}

void FilePickerDialog::setNameFilters(const QStringList& filters) {
    m_nameFilters = filters;
    updateFileList();
}

QString FilePickerDialog::selectedFile() const {
    return m_selectedFile;
}

QStringList FilePickerDialog::selectedFiles() const {
    return m_selectedFiles;
}

} // namespace havel
