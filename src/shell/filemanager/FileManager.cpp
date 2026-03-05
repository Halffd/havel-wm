// File Manager Implementation

#include "FileManager.hpp"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QCompleter>
#include <QDesktopServices>
#include <QMimeDatabase>
#include <QIcon>
#include <QKeySequence>
#include <QDateTime>

namespace havel {

FileManagerWindow::FileManagerWindow(const QString& startPath, QWidget* parent)
    : QMainWindow(parent)
    , m_directoryTree(nullptr)
    , m_fileList(nullptr)
    , m_fileModel(nullptr)
    , m_directoryModel(nullptr)
    , m_locationBar(nullptr)
    , m_statusLabel(nullptr)
    , m_selectedLabel(nullptr)
    , m_toolBar(nullptr)
    , m_fileMenu(nullptr)
    , m_editMenu(nullptr)
    , m_viewMenu(nullptr)
    , m_helpMenu(nullptr)
    , m_clipboardOp(ClipboardOp::None)
    , m_historyIndex(-1)
{
    setWindowTitle("File Manager - Havel WM");
    setMinimumSize(1024, 768);
    
    setupUI();
    setupActions();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupShortcuts();
    
    // Navigate to start path or home
    QString path = startPath.isEmpty() ? QDir::homePath() : startPath;
    navigateToPath(path);
}

FileManagerWindow::~FileManagerWindow() {
}

void FileManagerWindow::setupUI() {
    // Create file system models
    m_fileModel = new QFileSystemModel(this);
    m_fileModel->setRootPath("");
    m_fileModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    
    m_directoryModel = new QFileSystemModel(this);
    m_directoryModel->setRootPath("");
    m_directoryModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden);
    
    // Create directory tree (left panel)
    m_directoryTree = new QTreeView();
    m_directoryTree->setModel(m_directoryModel);
    m_directoryTree->setHeaderHidden(true);
    m_directoryTree->setAnimated(true);
    m_directoryTree->setIndentation(20);
    m_directoryTree->setSortingEnabled(true);
    m_directoryTree->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Hide all columns except name
    for (int i = 1; i < m_directoryModel->columnCount(); ++i) {
        m_directoryTree->hideColumn(i);
    }
    
    // Create file list (right panel)
    m_fileList = new QListView();
    m_fileList->setModel(m_fileModel);
    m_fileList->setViewMode(QListView::IconMode);
    m_fileList->setMovement(QListView::Static);
    m_fileList->setResizeMode(QListView::Adjust);
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fileList->setDragEnabled(true);
    m_fileList->setAcceptDrops(true);
    m_fileList->setDropIndicatorShown(true);
    m_fileList->setDefaultDropAction(Qt::CopyAction);
    
    // Set icon size
    m_fileList->setIconSize(QSize(64, 64));
    m_fileList->setGridSize(QSize(100, 90));
    m_fileList->setWrapping(true);
    m_fileList->setUniformItemSizes(true);
    
    // Create splitter
    QSplitter* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(m_directoryTree);
    splitter->addWidget(m_fileList);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes(QList<int>() << 200 << 600);
    
    setCentralWidget(splitter);
    
    // Connections
    connect(m_directoryTree, &QTreeView::clicked, this, &FileManagerWindow::onDirectoryClicked);
    connect(m_fileList, &QListView::doubleClicked, this, &FileManagerWindow::onFileDoubleClicked);
    connect(m_fileList, &QListView::clicked, this, &FileManagerWindow::onFileClicked);
    connect(m_fileList, &QListView::customContextMenuRequested, this, &FileManagerWindow::showContextMenu);
    connect(m_directoryTree, &QTreeView::customContextMenuRequested, this, &FileManagerWindow::showDirectoryContextMenu);
}

void FileManagerWindow::setupActions() {
    // Navigation actions
    m_backAction = new QAction(QIcon::fromTheme("go-previous"), "Back", this);
    m_backAction->setShortcut(QKeySequence::Back);
    m_backAction->setStatusTip("Go back");
    connect(m_backAction, &QAction::triggered, this, &FileManagerWindow::navigateBack);
    
    m_forwardAction = new QAction(QIcon::fromTheme("go-next"), "Forward", this);
    m_forwardAction->setShortcut(QKeySequence::Forward);
    m_forwardAction->setStatusTip("Go forward");
    connect(m_forwardAction, &QAction::triggered, this, &FileManagerWindow::navigateForward);
    
    m_upAction = new QAction(QIcon::fromTheme("go-up"), "Up", this);
    m_upAction->setShortcut(Qt::ALT | Qt::Key_Up);
    m_upAction->setStatusTip("Go to parent directory");
    connect(m_upAction, &QAction::triggered, this, &FileManagerWindow::navigateUp);
    
    m_homeAction = new QAction(QIcon::fromTheme("go-home"), "Home", this);
    m_homeAction->setShortcut(Qt::CTRL | Qt::Key_H);
    m_homeAction->setStatusTip("Go to home directory");
    connect(m_homeAction, &QAction::triggered, this, &FileManagerWindow::navigateHome);
    
    m_refreshAction = new QAction(QIcon::fromTheme("view-refresh"), "Refresh", this);
    m_refreshAction->setShortcut(QKeySequence::Refresh);
    m_refreshAction->setStatusTip("Refresh view");
    connect(m_refreshAction, &QAction::triggered, this, &FileManagerWindow::refresh);
    
    // File operations
    m_newFileAction = new QAction(QIcon::fromTheme("document-new"), "New File", this);
    m_newFileAction->setShortcut(QKeySequence::New);
    m_newFileAction->setStatusTip("Create new file");
    connect(m_newFileAction, &QAction::triggered, this, &FileManagerWindow::newFile);
    
    m_newFolderAction = new QAction(QIcon::fromTheme("folder-new"), "New Folder", this);
    m_newFolderAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_N);
    m_newFolderAction->setStatusTip("Create new folder");
    connect(m_newFolderAction, &QAction::triggered, this, &FileManagerWindow::newFolder);
    
    m_copyAction = new QAction(QIcon::fromTheme("edit-copy"), "Copy", this);
    m_copyAction->setShortcut(QKeySequence::Copy);
    m_copyAction->setStatusTip("Copy selected files");
    connect(m_copyAction, &QAction::triggered, this, &FileManagerWindow::copyFile);
    
    m_cutAction = new QAction(QIcon::fromTheme("edit-cut"), "Cut", this);
    m_cutAction->setShortcut(QKeySequence::Cut);
    m_cutAction->setStatusTip("Cut selected files");
    connect(m_cutAction, &QAction::triggered, this, &FileManagerWindow::cutFile);
    
    m_pasteAction = new QAction(QIcon::fromTheme("edit-paste"), "Paste", this);
    m_pasteAction->setShortcut(QKeySequence::Paste);
    m_pasteAction->setStatusTip("Paste files");
    connect(m_pasteAction, &QAction::triggered, this, &FileManagerWindow::pasteFile);
    
    m_deleteAction = new QAction(QIcon::fromTheme("edit-delete"), "Delete", this);
    m_deleteAction->setShortcut(QKeySequence::Delete);
    m_deleteAction->setStatusTip("Delete selected files");
    connect(m_deleteAction, &QAction::triggered, this, &FileManagerWindow::deleteFile);
    
    m_renameAction = new QAction(QIcon::fromTheme("edit-rename"), "Rename", this);
    m_renameAction->setShortcut(Qt::Key_F2);
    m_renameAction->setStatusTip("Rename selected file");
    connect(m_renameAction, &QAction::triggered, this, &FileManagerWindow::renameFile);
    
    m_propertiesAction = new QAction(QIcon::fromTheme("document-properties"), "Properties", this);
    m_propertiesAction->setShortcut(Qt::ALT | Qt::Key_Return);
    m_propertiesAction->setStatusTip("Show properties");
    connect(m_propertiesAction, &QAction::triggered, this, &FileManagerWindow::properties);
    
    // Search
    m_searchAction = new QAction(QIcon::fromTheme("edit-find"), "Search", this);
    m_searchAction->setShortcut(QKeySequence::Find);
    m_searchAction->setStatusTip("Search files");
    connect(m_searchAction, &QAction::triggered, this, &FileManagerWindow::performSearch);
}

void FileManagerWindow::setupMenuBar() {
    m_fileMenu = menuBar()->addMenu("&File");
    m_fileMenu->addAction(m_newFileAction);
    m_fileMenu->addAction(m_newFolderAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_propertiesAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_deleteAction);
    
    m_editMenu = menuBar()->addMenu("&Edit");
    m_editMenu->addAction(m_copyAction);
    m_editMenu->addAction(m_cutAction);
    m_editMenu->addAction(m_pasteAction);
    m_editMenu->addSeparator();
    m_editMenu->addAction(m_renameAction);
    m_editMenu->addSeparator();
    m_editMenu->addAction(m_searchAction);
    
    m_viewMenu = menuBar()->addMenu("&View");
    m_viewMenu->addAction(m_refreshAction);
    
    m_helpMenu = menuBar()->addMenu("&Help");
    m_helpMenu->addAction("About", [this]() {
        QMessageBox::about(this, "About File Manager",
            "Havel WM File Manager\n\n"
            "A simple file manager for Havel WM desktop environment.");
    });
}

void FileManagerWindow::setupToolBar() {
    m_toolBar = addToolBar("Navigation");
    m_toolBar->setMovable(false);
    
    m_toolBar->addAction(m_backAction);
    m_toolBar->addAction(m_forwardAction);
    m_toolBar->addAction(m_upAction);
    m_toolBar->addAction(m_homeAction);
    
    // Location bar
    m_locationBar = new QLineEdit();
    m_locationBar->setPlaceholderText("Enter path...");
    m_locationBar->setClearButtonEnabled(true);
    
    // Add completer for path completion
    QCompleter* completer = new QCompleter(this);
    completer->setModel(m_directoryModel);
    m_locationBar->setCompleter(completer);
    
    connect(m_locationBar, &QLineEdit::returnPressed, [this]() {
        navigateToPath(m_locationBar->text());
    });
    
    m_toolBar->addWidget(m_locationBar);
    
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_refreshAction);
}

void FileManagerWindow::setupStatusBar() {
    m_statusLabel = new QLabel("Ready");
    m_selectedLabel = new QLabel("");
    
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_selectedLabel);
}

void FileManagerWindow::setupShortcuts() {
    // Alt+Up for parent directory
    QShortcut* altUp = new QShortcut(Qt::ALT | Qt::Key_Up, this);
    connect(altUp, &QShortcut::activated, this, &FileManagerWindow::navigateUp);
    
    // Ctrl+L for location bar focus
    QShortcut* ctrlL = new QShortcut(Qt::CTRL | Qt::Key_L, this);
    connect(ctrlL, &QShortcut::activated, [this]() {
        m_locationBar->setFocus();
        m_locationBar->selectAll();
    });
}

void FileManagerWindow::navigateToPath(const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists()) {
        statusBar()->showMessage("Path does not exist: " + path, 3000);
        return;
    }
    
    QString canonicalPath = fi.canonicalFilePath();
    
    // Update models
    m_fileModel->setRootPath(canonicalPath);
    m_directoryTree->setRootIndex(m_directoryModel->index(canonicalPath));
    m_fileList->setRootIndex(m_fileModel->index(canonicalPath));
    
    // Update location bar
    m_locationBar->setText(canonicalPath);
    
    // Update history
    if (m_historyIndex >= 0 && m_historyIndex < m_history.size() - 1) {
        m_history.erase(m_history.begin() + m_historyIndex + 1, m_history.end());
    }
    m_history.push_back(canonicalPath);
    m_historyIndex = m_history.size() - 1;
    
    m_currentPath = canonicalPath;
    
    updateNavigationButtons();
    updateStatusBar();
    
    statusBar()->showMessage("Navigated to: " + canonicalPath, 2000);
}

void FileManagerWindow::navigateUp() {
    QFileInfo fi(m_currentPath);
    if (fi.isDir() && fi.path() != fi.canonicalFilePath()) {
        navigateToPath(fi.path());
    }
}

void FileManagerWindow::navigateHome() {
    navigateToPath(QDir::homePath());
}

void FileManagerWindow::navigateBack() {
    if (m_historyIndex > 0) {
        m_historyIndex--;
        navigateToPath(m_history[m_historyIndex]);
    }
}

void FileManagerWindow::navigateForward() {
    if (m_historyIndex < m_history.size() - 1) {
        m_historyIndex++;
        navigateToPath(m_history[m_historyIndex]);
    }
}

void FileManagerWindow::onDirectoryClicked(const QModelIndex& index) {
    QString path = m_directoryModel->filePath(index);
    if (m_directoryModel->isDir(index)) {
        navigateToPath(path);
    }
}

void FileManagerWindow::onFileDoubleClicked(const QModelIndex& index) {
    QString path = m_fileModel->filePath(index);
    QFileInfo fi(path);
    
    if (fi.isDir()) {
        navigateToPath(path);
    } else {
        // Open file with default application
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void FileManagerWindow::onFileClicked(const QModelIndex& index) {
    updateStatusBar();
}

void FileManagerWindow::updateLocationBar(const QModelIndex& index) {
    QString path = m_fileModel->filePath(index);
    m_locationBar->setText(path);
}

void FileManagerWindow::newFile() {
    bool ok;
    QString name = QInputDialog::getText(this, "New File", "File name:", QLineEdit::Normal, "", &ok);
    
    if (ok && !name.isEmpty()) {
        QString path = m_currentPath + "/" + name;
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.close();
            statusBar()->showMessage("Created file: " + name, 2000);
        } else {
            QMessageBox::critical(this, "Error", "Failed to create file: " + name);
        }
    }
}

void FileManagerWindow::newFolder() {
    bool ok;
    QString name = QInputDialog::getText(this, "New Folder", "Folder name:", QLineEdit::Normal, "", &ok);
    
    if (ok && !name.isEmpty()) {
        QString path = m_currentPath + "/" + name;
        QDir dir(m_currentPath);
        if (dir.mkdir(name)) {
            statusBar()->showMessage("Created folder: " + name, 2000);
        } else {
            QMessageBox::critical(this, "Error", "Failed to create folder: " + name);
        }
    }
}

void FileManagerWindow::copyFile() {
    QModelIndexList selected = m_fileList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    
    m_clipboardFiles.clear();
    for (const QModelIndex& index : selected) {
        m_clipboardFiles.append(m_fileModel->filePath(index));
    }
    m_clipboardOp = ClipboardOp::Copy;
    
    statusBar()->showMessage(QString("Copied %1 file(s)").arg(m_clipboardFiles.size()), 2000);
}

void FileManagerWindow::cutFile() {
    QModelIndexList selected = m_fileList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    
    m_clipboardFiles.clear();
    for (const QModelIndex& index : selected) {
        m_clipboardFiles.append(m_fileModel->filePath(index));
    }
    m_clipboardOp = ClipboardOp::Cut;
    
    statusBar()->showMessage(QString("Cut %1 file(s)").arg(m_clipboardFiles.size()), 2000);
}

void FileManagerWindow::pasteFile() {
    if (m_clipboardFiles.isEmpty()) return;
    
    for (const QString& src : m_clipboardFiles) {
        QFileInfo fi(src);
        QString dst = m_currentPath + "/" + fi.fileName();
        
        if (m_clipboardOp == ClipboardOp::Copy) {
            copyFileInternal(src, dst);
        } else if (m_clipboardOp == ClipboardOp::Cut) {
            QFile::rename(src, dst);
        }
    }
    
    if (m_clipboardOp == ClipboardOp::Cut) {
        m_clipboardFiles.clear();
        m_clipboardOp = ClipboardOp::None;
    }
    
    refresh();
    statusBar()->showMessage("Paste complete", 2000);
}

void FileManagerWindow::copyFileInternal(const QString& src, const QString& dst) {
    QFile srcFile(src);
    if (!srcFile.copy(dst)) {
        QMessageBox::critical(this, "Error", "Failed to copy: " + src);
    }
}

void FileManagerWindow::deleteFile() {
    QModelIndexList selected = m_fileList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    
    QStringList files;
    for (const QModelIndex& index : selected) {
        files.append(m_fileModel->filePath(index));
    }
    
    if (!confirmDelete(files)) return;
    
    for (const QString& file : files) {
        QFileInfo fi(file);
        if (fi.isDir()) {
            QDir(file).removeRecursively();
        } else {
            QFile::remove(file);
        }
    }
    
    refresh();
    statusBar()->showMessage(QString("Deleted %1 file(s)").arg(files.size()), 2000);
}

bool FileManagerWindow::confirmDelete(const QStringList& files) {
    QString message = files.size() == 1 ? 
        QString("Delete \"%1\"?").arg(files[0]) :
        QString("Delete %1 files?").arg(files.size());
    
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Confirm Delete", message,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    
    return reply == QMessageBox::Yes;
}

void FileManagerWindow::renameFile() {
    QModelIndexList selected = m_fileList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    
    QString oldPath = m_fileModel->filePath(selected.first());
    QFileInfo fi(oldPath);
    
    bool ok;
    QString newName = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, fi.fileName(), &ok);
    
    if (ok && !newName.isEmpty() && newName != fi.fileName()) {
        QString newPath = fi.path() + "/" + newName;
        if (QFile::rename(oldPath, newPath)) {
            statusBar()->showMessage("Renamed to: " + newName, 2000);
        } else {
            QMessageBox::critical(this, "Error", "Failed to rename file");
        }
    }
}

void FileManagerWindow::properties() {
    QModelIndexList selected = m_fileList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    
    QString path = m_fileModel->filePath(selected.first());
    QFileInfo fi(path);
    
    QString info = QString(
        "Name: %1\n"
        "Path: %2\n"
        "Type: %3\n"
        "Size: %4 bytes\n"
        "Modified: %5\n"
        "Readable: %6\n"
        "Writable: %7"
    )
    .arg(fi.fileName())
    .arg(fi.canonicalFilePath())
    .arg(fi.isDir() ? "Directory" : "File")
    .arg(fi.size())
    .arg(fi.lastModified().toString())
    .arg(fi.isReadable() ? "Yes" : "No")
    .arg(fi.isWritable() ? "Yes" : "No");
    
    QMessageBox::information(this, "Properties", info);
}

void FileManagerWindow::refresh() {
    navigateToPath(m_currentPath);
    statusBar()->showMessage("Refreshed", 1000);
}

void FileManagerWindow::performSearch() {
    bool ok;
    QString searchTerm = QInputDialog::getText(this, "Search", "Search for:", QLineEdit::Normal, "", &ok);
    
    if (ok && !searchTerm.isEmpty()) {
        // Simple search in current directory
        QDir dir(m_currentPath);
        QStringList filters;
        filters << "*" + searchTerm + "*";
        dir.setNameFilters(filters);
        
        QFileInfoList files = dir.entryInfoList(filters, QDir::Files | QDir::Dirs, QDir::Name);
        
        if (files.isEmpty()) {
            QMessageBox::information(this, "Search", "No files found matching: " + searchTerm);
        } else {
            QString results;
            for (const QFileInfo& fi : files) {
                results += fi.fileName() + "\n";
            }
            QMessageBox::information(this, "Search Results", "Found " + QString::number(files.size()) + " files:\n\n" + results);
        }
    }
}

void FileManagerWindow::showContextMenu(const QPoint& pos) {
    QMenu contextMenu(this);
    
    QModelIndex index = m_fileList->indexAt(pos);
    if (index.isValid()) {
        // File context menu
        contextMenu.addAction(m_copyAction);
        contextMenu.addAction(m_cutAction);
        contextMenu.addAction(m_pasteAction);
        contextMenu.addSeparator();
        contextMenu.addAction(m_renameAction);
        contextMenu.addAction(m_deleteAction);
        contextMenu.addSeparator();
        contextMenu.addAction(m_propertiesAction);
    } else {
        // Empty space context menu
        contextMenu.addAction(m_newFileAction);
        contextMenu.addAction(m_newFolderAction);
        contextMenu.addSeparator();
        contextMenu.addAction(m_pasteAction);
        contextMenu.addSeparator();
        contextMenu.addAction(m_refreshAction);
        contextMenu.addAction(m_searchAction);
    }
    
    contextMenu.exec(m_fileList->mapToGlobal(pos));
}

void FileManagerWindow::showDirectoryContextMenu(const QPoint& pos) {
    QMenu contextMenu(this);
    contextMenu.addAction(m_newFolderAction);
    contextMenu.addSeparator();
    contextMenu.addAction(m_refreshAction);
    contextMenu.exec(m_directoryTree->mapToGlobal(pos));
}

void FileManagerWindow::updateStatusBar() {
    QModelIndexList selected = m_fileList->selectionModel()->selectedIndexes();
    
    if (selected.isEmpty()) {
        m_selectedLabel->setText("");
    } else {
        int count = selected.size();
        if (count == 1) {
            QString path = m_fileModel->filePath(selected.first());
            QFileInfo fi(path);
            m_selectedLabel->setText(fi.fileName());
        } else {
            m_selectedLabel->setText(QString("%1 items selected").arg(count));
        }
    }
    
    // Count total items
    QDir dir(m_currentPath);
    int total = dir.count() - 2;  // Exclude . and ..
    m_statusLabel->setText(QString("%1 items").arg(total));
}

void FileManagerWindow::updateNavigationButtons() {
    m_backAction->setEnabled(m_historyIndex > 0);
    m_forwardAction->setEnabled(m_historyIndex < m_history.size() - 1);
    m_upAction->setEnabled(m_currentPath != "/" && m_currentPath != QDir::homePath());
}

} // namespace havel

#include "FileManager.moc"
