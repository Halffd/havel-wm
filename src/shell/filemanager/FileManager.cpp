// File Manager Implementation - Advanced Features

#include "FileManager.hpp"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QCompleter>
#include <QDesktopServices>
#include <QMimeDatabase>
#include <QMimeType>
#include <QIcon>
#include <QKeySequence>
#include <QDateTime>
#include <QDirIterator>
#include <QStyle>
#include <QRegularExpression>

namespace havel {

// ============================================================================
// FileSortProxyModel Implementation
// ============================================================================

FileSortProxyModel::FileSortProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
    , m_sortMode(SortMode::Name)
    , m_customOrder(SortOrder::Ascending)
    , m_groupMode(GroupMode::None)
{
}

void FileSortProxyModel::setSortMode(SortMode mode) {
    m_sortMode = mode;
    invalidate();
}

void FileSortProxyModel::setCustomSortOrder(SortOrder order) {
    m_customOrder = order;
    invalidate();
}

void FileSortProxyModel::setGroupMode(GroupMode mode) {
    m_groupMode = mode;
    invalidate();
}

bool FileSortProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    QFileSystemModel* fsm = qobject_cast<QFileSystemModel*>(sourceModel());
    if (!fsm) return QSortFilterProxyModel::lessThan(left, right);
    
    bool leftIsDir = fsm->isDir(left);
    bool rightIsDir = fsm->isDir(right);
    
    // Directories always come first
    if (leftIsDir != rightIsDir) {
        return leftIsDir;
    }
    
    int result = 0;
    
    switch (m_sortMode) {
        case SortMode::Name:
            result = QString::compare(
                fsm->fileName(left),
                fsm->fileName(right),
                Qt::CaseInsensitive);
            break;
            
        case SortMode::Size:
            result = qint64(fsm->size(left)) - qint64(fsm->size(right));
            break;
            
        case SortMode::Type:
            result = QString::compare(
                fsm->type(left),
                fsm->type(right),
                Qt::CaseInsensitive);
            break;
            
        case SortMode::DateModified:
            result = fsm->lastModified(left).secsTo(fsm->lastModified(right));
            break;
            
        case SortMode::DateCreated:
            result = fsm->lastModified(left).secsTo(fsm->lastModified(right));
            break;
            
        case SortMode::Extension:
            result = QString::compare(
                getFileExtension(fsm->fileName(left)),
                getFileExtension(fsm->fileName(right)),
                Qt::CaseInsensitive);
            break;
    }
    
    return (m_customOrder == SortOrder::Ascending) ? (result < 0) : (result > 0);
}

QVariant FileSortProxyModel::data(const QModelIndex& index, int role) const {
    if (m_groupMode != GroupMode::None && role == Qt::FontRole) {
        QFont font = QSortFilterProxyModel::data(index, role).value<QFont>();
        // Could make group headers bold
        return font;
    }
    return QSortFilterProxyModel::data(index, role);
}

QString FileSortProxyModel::getFileExtension(const QString& fileName) const {
    int dotPos = fileName.lastIndexOf('.');
    if (dotPos > 0) {
        return fileName.mid(dotPos + 1).toLower();
    }
    return "";
}

QString FileSortProxyModel::getFileType(const QString& filePath) const {
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(filePath);
    return mime.name();
}

int FileSortProxyModel::compareFiles(const QModelIndex& left, const QModelIndex& right) const {
    QFileSystemModel* fsm = qobject_cast<QFileSystemModel*>(sourceModel());
    if (!fsm) return 0;
    
    bool leftIsDir = fsm->isDir(left);
    bool rightIsDir = fsm->isDir(right);
    
    if (leftIsDir != rightIsDir) {
        return leftIsDir ? -1 : 1;
    }
    
    return lessThan(left, right) ? -1 : 1;
}

// ============================================================================
// FileManagerWindow Implementation
// ============================================================================

FileManagerWindow::FileManagerWindow(const QString& startPath, QWidget* parent)
    : QMainWindow(parent)
    , m_tabWidget(nullptr)
    , m_mainToolBar(nullptr)
    , m_viewToolBar(nullptr)
    , m_locationBar(nullptr)
    , m_searchBar(nullptr)
    , m_statusLabel(nullptr)
    , m_selectedLabel(nullptr)
    , m_sortComboBox(nullptr)
    , m_groupComboBox(nullptr)
    , m_fileModel(nullptr)
    , m_directoryModel(nullptr)
    , m_clipboardOp(ClipboardOp::None)
    , m_currentSortMode(SortMode::Name)
    , m_currentSortOrder(SortOrder::Ascending)
    , m_currentGroupMode(GroupMode::None)
    , m_currentViewMode(0)
{
    setWindowTitle("File Manager - Havel WM");
    setMinimumSize(1280, 800);
    
    setupUI();
    setupActions();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupShortcuts();
    
    // Create first tab
    QString path = startPath.isEmpty() ? QDir::homePath() : startPath;
    newTab(path);
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
    
    // Create tab widget
    m_tabWidget = new QTabWidget();
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);
    
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &FileManagerWindow::closeTab);
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &FileManagerWindow::currentTabChanged);
    
    setCentralWidget(m_tabWidget);
}

void FileManagerWindow::setupActions() {
    // Tab actions
    m_newTabAction = new QAction(QIcon::fromTheme("tab-new"), "New Tab", this);
    m_newTabAction->setShortcut(QKeySequence::AddTab);
    m_newTabAction->setStatusTip("Open new tab");
    connect(m_newTabAction, &QAction::triggered, [this]() { newTab(); });
    
    m_closeTabAction = new QAction(QIcon::fromTheme("window-close"), "Close Tab", this);
    m_closeTabAction->setShortcut(QKeySequence::Close);
    m_closeTabAction->setStatusTip("Close current tab");
    connect(m_closeTabAction, &QAction::triggered, [this]() {
        closeTab(m_tabWidget->currentIndex());
    });
    
    m_duplicateTabAction = new QAction("Duplicate Tab", this);
    m_duplicateTabAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_T);
    m_duplicateTabAction->setStatusTip("Duplicate current tab");
    connect(m_duplicateTabAction, &QAction::triggered, this, &FileManagerWindow::duplicateTab);
    
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
    
    // View mode actions
    m_viewIconsAction = new QAction("Icons", this);
    m_viewIconsAction->setCheckable(true);
    m_viewIconsAction->setChecked(true);
    connect(m_viewIconsAction, &QAction::triggered, [this]() { setViewMode(0); });
    
    m_viewListAction = new QAction("List", this);
    m_viewListAction->setCheckable(true);
    connect(m_viewListAction, &QAction::triggered, [this]() { setViewMode(1); });
    
    m_viewDetailsAction = new QAction("Details", this);
    m_viewDetailsAction->setCheckable(true);
    connect(m_viewDetailsAction, &QAction::triggered, [this]() { setViewMode(2); });
    
    // Sort actions
    m_sortByNameAction = new QAction("Name", this);
    m_sortByNameAction->setCheckable(true);
    m_sortByNameAction->setChecked(true);
    connect(m_sortByNameAction, &QAction::triggered, [this]() { setSortMode(SortMode::Name); });
    
    m_sortBySizeAction = new QAction("Size", this);
    m_sortBySizeAction->setCheckable(true);
    connect(m_sortBySizeAction, &QAction::triggered, [this]() { setSortMode(SortMode::Size); });
    
    m_sortByTypeAction = new QAction("Type", this);
    m_sortByTypeAction->setCheckable(true);
    connect(m_sortByTypeAction, &QAction::triggered, [this]() { setSortMode(SortMode::Type); });
    
    m_sortByDateAction = new QAction("Date Modified", this);
    m_sortByDateAction->setCheckable(true);
    connect(m_sortByDateAction, &QAction::triggered, [this]() { setSortMode(SortMode::DateModified); });
    
    m_sortAscendingAction = new QAction("Ascending", this);
    m_sortAscendingAction->setCheckable(true);
    m_sortAscendingAction->setChecked(true);
    connect(m_sortAscendingAction, &QAction::triggered, [this]() {
        m_currentSortOrder = SortOrder::Ascending;
        toggleSortOrder();
    });
    
    m_sortDescendingAction = new QAction("Descending", this);
    m_sortDescendingAction->setCheckable(true);
    connect(m_sortDescendingAction, &QAction::triggered, [this]() {
        m_currentSortOrder = SortOrder::Descending;
        toggleSortOrder();
    });
    
    // Group actions
    m_groupNoneAction = new QAction("None", this);
    m_groupNoneAction->setCheckable(true);
    m_groupNoneAction->setChecked(true);
    connect(m_groupNoneAction, &QAction::triggered, [this]() { setGroupMode(GroupMode::None); });
    
    m_groupByTypeAction = new QAction("Type", this);
    m_groupByTypeAction->setCheckable(true);
    connect(m_groupByTypeAction, &QAction::triggered, [this]() { setGroupMode(GroupMode::Type); });
    
    m_groupByDateAction = new QAction("Date", this);
    m_groupByDateAction->setCheckable(true);
    connect(m_groupByDateAction, &QAction::triggered, [this]() { setGroupMode(GroupMode::Date); });
    
    m_groupBySizeAction = new QAction("Size", this);
    m_groupBySizeAction->setCheckable(true);
    connect(m_groupBySizeAction, &QAction::triggered, [this]() { setGroupMode(GroupMode::Size); });
    
    // Search
    m_searchAction = new QAction(QIcon::fromTheme("edit-find"), "Search", this);
    m_searchAction->setShortcut(QKeySequence::Find);
    m_searchAction->setStatusTip("Search files");
    connect(m_searchAction, &QAction::triggered, this, &FileManagerWindow::performSearch);
}

void FileManagerWindow::setupMenuBar() {
    m_fileMenu = menuBar()->addMenu("&File");
    m_fileMenu->addAction(m_newTabAction);
    m_fileMenu->addAction(m_closeTabAction);
    m_fileMenu->addAction(m_duplicateTabAction);
    m_fileMenu->addSeparator();
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
    
    // View mode submenu
    QMenu* viewModeMenu = m_viewMenu->addMenu("View Mode");
    viewModeMenu->addAction(m_viewIconsAction);
    viewModeMenu->addAction(m_viewListAction);
    viewModeMenu->addAction(m_viewDetailsAction);
    
    // Sort submenu
    m_sortMenu = m_viewMenu->addMenu("Sort By");
    m_sortMenu->addAction(m_sortByNameAction);
    m_sortMenu->addAction(m_sortBySizeAction);
    m_sortMenu->addAction(m_sortByTypeAction);
    m_sortMenu->addAction(m_sortByDateAction);
    m_sortMenu->addSeparator();
    m_sortMenu->addAction(m_sortAscendingAction);
    m_sortMenu->addAction(m_sortDescendingAction);
    
    // Group submenu
    m_groupMenu = m_viewMenu->addMenu("Group By");
    m_groupMenu->addAction(m_groupNoneAction);
    m_groupMenu->addAction(m_groupByTypeAction);
    m_groupMenu->addAction(m_groupByDateAction);
    m_groupMenu->addAction(m_groupBySizeAction);
    
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_refreshAction);
    
    m_helpMenu = menuBar()->addMenu("&Help");
    m_helpMenu->addAction("About", [this]() {
        QMessageBox::about(this, "About File Manager",
            "Havel WM File Manager\n\n"
            "Advanced file manager with tabs, sorting, and grouping.\n\n"
            "Features:\n"
            "- Multiple tabs\n"
            "- Sort by name, size, type, date\n"
            "- Group files by type, date, size\n"
            "- Icon, list, and details views\n"
            "- Search and filter");
    });
}

void FileManagerWindow::setupToolBar() {
    // Main toolbar
    m_mainToolBar = addToolBar("Navigation");
    m_mainToolBar->setMovable(false);
    
    m_mainToolBar->addAction(m_backAction);
    m_mainToolBar->addAction(m_forwardAction);
    m_mainToolBar->addAction(m_upAction);
    m_mainToolBar->addAction(m_homeAction);
    
    // Location bar
    m_locationBar = new QLineEdit();
    m_locationBar->setPlaceholderText("Enter path...");
    m_locationBar->setClearButtonEnabled(true);
    m_locationBar->setMinimumWidth(300);
    
    QCompleter* completer = new QCompleter(this);
    completer->setModel(m_directoryModel);
    m_locationBar->setCompleter(completer);
    
    connect(m_locationBar, &QLineEdit::returnPressed, [this]() {
        navigateToPath(m_locationBar->text());
    });
    
    m_mainToolBar->addWidget(m_locationBar);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_refreshAction);
    
    // View toolbar
    m_viewToolBar = addToolBar("View");
    m_viewToolBar->setMovable(false);
    
    // Sort combo
    m_sortComboBox = new QComboBox();
    m_sortComboBox->addItem("Sort: Name");
    m_sortComboBox->addItem("Sort: Size");
    m_sortComboBox->addItem("Sort: Type");
    m_sortComboBox->addItem("Sort: Date");
    connect(m_sortComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
        switch (index) {
            case 0: setSortMode(SortMode::Name); break;
            case 1: setSortMode(SortMode::Size); break;
            case 2: setSortMode(SortMode::Type); break;
            case 3: setSortMode(SortMode::DateModified); break;
        }
    });
    m_viewToolBar->addWidget(m_sortComboBox);
    
    m_viewToolBar->addSeparator();
    
    // Group combo
    m_groupComboBox = new QComboBox();
    m_groupComboBox->addItem("Group: None");
    m_groupComboBox->addItem("Group: Type");
    m_groupComboBox->addItem("Group: Date");
    m_groupComboBox->addItem("Group: Size");
    connect(m_groupComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
        switch (index) {
            case 0: setGroupMode(GroupMode::None); break;
            case 1: setGroupMode(GroupMode::Type); break;
            case 2: setGroupMode(GroupMode::Date); break;
            case 3: setGroupMode(GroupMode::Size); break;
        }
    });
    m_viewToolBar->addWidget(m_groupComboBox);
    
    m_viewToolBar->addSeparator();
    
    // Search bar
    m_searchBar = new QLineEdit();
    m_searchBar->setPlaceholderText("Filter files...");
    m_searchBar->setClearButtonEnabled(true);
    m_searchBar->setMaximumWidth(200);
    connect(m_searchBar, &QLineEdit::textChanged, this, &FileManagerWindow::filterFiles);
    m_viewToolBar->addWidget(m_searchBar);
}

void FileManagerWindow::setupStatusBar() {
    m_statusLabel = new QLabel("Ready");
    m_selectedLabel = new QLabel("");
    
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_selectedLabel);
}

void FileManagerWindow::setupShortcuts() {
    // Ctrl+T for new tab
    QShortcut* ctrlT = new QShortcut(QKeySequence::AddTab, this);
    connect(ctrlT, &QShortcut::activated, [this]() { newTab(); });
    
    // Ctrl+W for close tab
    QShortcut* ctrlW = new QShortcut(QKeySequence::Close, this);
    connect(ctrlW, &QShortcut::activated, [this]() {
        closeTab(m_tabWidget->currentIndex());
    });
    
    // Ctrl+L for location bar
    QShortcut* ctrlL = new QShortcut(Qt::CTRL | Qt::Key_L, this);
    connect(ctrlL, &QShortcut::activated, [this]() {
        m_locationBar->setFocus();
        m_locationBar->selectAll();
    });
    
    // Ctrl+F for search
    QShortcut* ctrlF = new QShortcut(QKeySequence::Find, this);
    connect(ctrlF, &QShortcut::activated, [this]() {
        m_searchBar->setFocus();
        m_searchBar->selectAll();
    });
}

void FileManagerWindow::createFileView(int tabIndex) {
    QListView* fileList = new QListView();
    fileList->setViewMode(QListView::IconMode);
    fileList->setMovement(QListView::Static);
    fileList->setResizeMode(QListView::Adjust);
    fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    fileList->setDragEnabled(true);
    fileList->setAcceptDrops(true);
    fileList->setDropIndicatorShown(true);
    fileList->setDefaultDropAction(Qt::CopyAction);
    fileList->setIconSize(QSize(64, 64));
    fileList->setGridSize(QSize(100, 90));
    fileList->setWrapping(true);
    fileList->setUniformItemSizes(true);
    
    // Create proxy model for sorting
    FileSortProxyModel* proxyModel = new FileSortProxyModel(this);
    proxyModel->setSourceModel(m_fileModel);
    proxyModel->setSortMode(m_currentSortMode);
    proxyModel->setCustomSortOrder(m_currentSortOrder);
    proxyModel->setGroupMode(m_currentGroupMode);
    m_proxyModels.append(proxyModel);
    
    fileList->setModel(proxyModel);
    
    // Connections
    connect(fileList, &QListView::doubleClicked, this, &FileManagerWindow::onFileDoubleClicked);
    connect(fileList, &QListView::clicked, this, &FileManagerWindow::onFileClicked);
    connect(fileList, &QListView::customContextMenuRequested, this, &FileManagerWindow::showContextMenu);
    
    m_tabWidget->addTab(fileList, "Tab " + QString::number(tabIndex + 1));
}

void FileManagerWindow::newTab(const QString& path) {
    int tabIndex = m_tabWidget->count();
    createFileView(tabIndex);
    
    TabData tabData;
    tabData.path = path.isEmpty() ? QDir::homePath() : path;
    m_tabData.append(tabData);
    
    // Initialize history for this tab
    m_tabHistory.append(QVector<QString>());
    m_tabHistoryIndex.append(-1);
    
    m_tabWidget->setCurrentIndex(tabIndex);
    
    navigateToPath(tabData.path);
}

void FileManagerWindow::closeTab(int index) {
    if (m_tabWidget->count() == 1) {
        // Don't close last tab, just navigate home
        navigateHome();
        return;
    }
    
    m_tabWidget->removeTab(index);
    m_tabData.removeAt(index);
    m_tabHistory.removeAt(index);
    m_tabHistoryIndex.removeAt(index);
    
    if (index < m_proxyModels.size()) {
        m_proxyModels.removeAt(index);
    }
}

void FileManagerWindow::currentTabChanged(int index) {
    if (index < 0 || index >= m_tabData.size()) return;
    
    // Update location bar
    m_locationBar->setText(m_tabData[index].path);
    
    // Update navigation buttons
    updateNavigationButtons();
    updateStatusBar();
}

void FileManagerWindow::duplicateTab() {
    int currentIndex = m_tabWidget->currentIndex();
    if (currentIndex >= 0 && currentIndex < m_tabData.size()) {
        newTab(m_tabData[currentIndex].path);
    }
}

void FileManagerWindow::navigateToPath(const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists()) {
        statusBar()->showMessage("Path does not exist: " + path, 3000);
        return;
    }
    
    QString canonicalPath = fi.canonicalFilePath();
    int tabIndex = m_tabWidget->currentIndex();
    
    if (tabIndex < 0 || tabIndex >= m_tabData.size()) return;
    
    // Update tab data
    m_tabData[tabIndex].path = canonicalPath;
    
    // Update file view
    QListView* fileList = qobject_cast<QListView*>(m_tabWidget->currentWidget());
    if (fileList) {
        fileList->setRootIndex(m_proxyModels[tabIndex]->mapFromSource(
            m_fileModel->index(canonicalPath)));
    }
    
    // Update location bar
    m_locationBar->setText(canonicalPath);
    
    // Update history for this tab
    if (m_tabHistoryIndex[tabIndex] >= 0 && 
        m_tabHistoryIndex[tabIndex] < m_tabHistory[tabIndex].size() - 1) {
        m_tabHistory[tabIndex].erase(
            m_tabHistory[tabIndex].begin() + m_tabHistoryIndex[tabIndex] + 1,
            m_tabHistory[tabIndex].end());
    }
    m_tabHistory[tabIndex].push_back(canonicalPath);
    m_tabHistoryIndex[tabIndex] = m_tabHistory[tabIndex].size() - 1;
    
    updateNavigationButtons();
    updateStatusBar();
    
    statusBar()->showMessage("Navigated to: " + canonicalPath, 2000);
}

void FileManagerWindow::navigateUp() {
    int tabIndex = m_tabWidget->currentIndex();
    if (tabIndex < 0 || tabIndex >= m_tabData.size()) return;
    
    QFileInfo fi(m_tabData[tabIndex].path);
    if (fi.isDir() && fi.path() != fi.canonicalFilePath()) {
        navigateToPath(fi.path());
    }
}

void FileManagerWindow::navigateHome() {
    int tabIndex = m_tabWidget->currentIndex();
    if (tabIndex >= 0 && tabIndex < m_tabData.size()) {
        navigateToPath(QDir::homePath());
    }
}

void FileManagerWindow::navigateBack() {
    int tabIndex = m_tabWidget->currentIndex();
    if (tabIndex < 0 || tabIndex >= m_tabHistory.size()) return;
    
    if (m_tabHistoryIndex[tabIndex] > 0) {
        m_tabHistoryIndex[tabIndex]--;
        navigateToPath(m_tabHistory[tabIndex][m_tabHistoryIndex[tabIndex]]);
    }
}

void FileManagerWindow::navigateForward() {
    int tabIndex = m_tabWidget->currentIndex();
    if (tabIndex < 0 || tabIndex >= m_tabHistory.size()) return;
    
    if (m_tabHistoryIndex[tabIndex] < m_tabHistory[tabIndex].size() - 1) {
        m_tabHistoryIndex[tabIndex]++;
        navigateToPath(m_tabHistory[tabIndex][m_tabHistoryIndex[tabIndex]]);
    }
}

void FileManagerWindow::setSortMode(SortMode mode) {
    m_currentSortMode = mode;
    
    int tabIndex = m_tabWidget->currentIndex();
    if (tabIndex >= 0 && tabIndex < m_proxyModels.size()) {
        m_proxyModels[tabIndex]->setSortMode(mode);
    }
    
    updateSortMenu();
    statusBar()->showMessage("Sorted by: " + QString::number(static_cast<int>(mode)), 2000);
}

void FileManagerWindow::toggleSortOrder() {
    int tabIndex = m_tabWidget->currentIndex();
    if (tabIndex >= 0 && tabIndex < m_proxyModels.size()) {
        m_proxyModels[tabIndex]->setCustomSortOrder(m_currentSortOrder);
    }
    
    m_sortAscendingAction->setChecked(m_currentSortOrder == SortOrder::Ascending);
    m_sortDescendingAction->setChecked(m_currentSortOrder == SortOrder::Descending);
}

void FileManagerWindow::setGroupMode(GroupMode mode) {
    m_currentGroupMode = mode;
    
    int tabIndex = m_tabWidget->currentIndex();
    if (tabIndex >= 0 && tabIndex < m_proxyModels.size()) {
        m_proxyModels[tabIndex]->setGroupMode(mode);
    }
    
    updateGroupMenu();
    statusBar()->showMessage("Grouped by: " + QString::number(static_cast<int>(mode)), 2000);
}

void FileManagerWindow::updateSortMenu() {
    m_sortByNameAction->setChecked(m_currentSortMode == SortMode::Name);
    m_sortBySizeAction->setChecked(m_currentSortMode == SortMode::Size);
    m_sortByTypeAction->setChecked(m_currentSortMode == SortMode::Type);
    m_sortByDateAction->setChecked(m_currentSortMode == SortMode::DateModified);
    
    m_sortComboBox->setCurrentIndex(static_cast<int>(m_currentSortMode));
}

void FileManagerWindow::updateGroupMenu() {
    m_groupNoneAction->setChecked(m_currentGroupMode == GroupMode::None);
    m_groupByTypeAction->setChecked(m_currentGroupMode == GroupMode::Type);
    m_groupByDateAction->setChecked(m_currentGroupMode == GroupMode::Date);
    m_groupBySizeAction->setChecked(m_currentGroupMode == GroupMode::Size);
    
    m_groupComboBox->setCurrentIndex(static_cast<int>(m_currentGroupMode));
}

void FileManagerWindow::setViewMode(int mode) {
    m_currentViewMode = mode;
    
    QListView* fileList = qobject_cast<QListView*>(m_tabWidget->currentWidget());
    if (!fileList) return;
    
    switch (mode) {
        case 0:  // Icons
            fileList->setViewMode(QListView::IconMode);
            fileList->setIconSize(QSize(64, 64));
            fileList->setGridSize(QSize(100, 90));
            fileList->setWrapping(true);
            break;
        case 1:  // List
            fileList->setViewMode(QListView::ListMode);
            fileList->setIconSize(QSize(32, 32));
            fileList->setGridSize(QSize(200, 40));
            fileList->setWrapping(false);
            break;
        case 2:  // Details (would need QTreeView for full details)
            fileList->setViewMode(QListView::ListMode);
            fileList->setIconSize(QSize(24, 24));
            fileList->setGridSize(QSize(400, 30));
            fileList->setWrapping(false);
            break;
    }
    
    m_viewIconsAction->setChecked(mode == 0);
    m_viewListAction->setChecked(mode == 1);
    m_viewDetailsAction->setChecked(mode == 2);
}

void FileManagerWindow::onDirectoryClicked(const QModelIndex& index) {
    QString path = m_directoryModel->filePath(index);
    if (m_directoryModel->isDir(index)) {
        navigateToPath(path);
    }
}

void FileManagerWindow::onFileDoubleClicked(const QModelIndex& index) {
    QListView* fileList = qobject_cast<QListView*>(sender());
    if (!fileList) return;
    
    FileSortProxyModel* proxy = qobject_cast<FileSortProxyModel*>(fileList->model());
    if (!proxy) return;
    
    QModelIndex sourceIndex = proxy->mapToSource(index);
    QString path = m_fileModel->filePath(sourceIndex);
    QFileInfo fi(path);
    
    if (fi.isDir()) {
        navigateToPath(path);
    } else {
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

// ... (rest of file operations remain the same as before)
// For brevity, I'll include the key methods

void FileManagerWindow::newFile() {
    bool ok;
    QString name = QInputDialog::getText(this, "New File", "File name:", QLineEdit::Normal, "", &ok);
    
    if (ok && !name.isEmpty()) {
        int tabIndex = m_tabWidget->currentIndex();
        if (tabIndex < 0 || tabIndex >= m_tabData.size()) return;
        
        QString path = m_tabData[tabIndex].path + "/" + name;
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.close();
            statusBar()->showMessage("Created file: " + name, 2000);
            refresh();
        } else {
            QMessageBox::critical(this, "Error", "Failed to create file: " + name);
        }
    }
}

void FileManagerWindow::newFolder() {
    bool ok;
    QString name = QInputDialog::getText(this, "New Folder", "Folder name:", QLineEdit::Normal, "", &ok);
    
    if (ok && !name.isEmpty()) {
        int tabIndex = m_tabWidget->currentIndex();
        if (tabIndex < 0 || tabIndex >= m_tabData.size()) return;
        
        QString path = m_tabData[tabIndex].path + "/" + name;
        QDir dir(m_tabData[tabIndex].path);
        if (dir.mkdir(name)) {
            statusBar()->showMessage("Created folder: " + name, 2000);
            refresh();
        } else {
            QMessageBox::critical(this, "Error", "Failed to create folder: " + name);
        }
    }
}

void FileManagerWindow::refresh() {
    int tabIndex = m_tabWidget->currentIndex();
    if (tabIndex >= 0 && tabIndex < m_tabData.size()) {
        navigateToPath(m_tabData[tabIndex].path);
    }
    statusBar()->showMessage("Refreshed", 1000);
}

void FileManagerWindow::filterFiles(const QString& text) {
    int tabIndex = m_tabWidget->currentIndex();
    if (tabIndex < 0 || tabIndex >= m_proxyModels.size()) return;
    
    if (text.isEmpty()) {
        m_proxyModels[tabIndex]->setFilterRegularExpression(QRegularExpression());
    } else {
        QRegularExpression re(text, QRegularExpression::CaseInsensitiveOption);
        m_proxyModels[tabIndex]->setFilterRegularExpression(re);
    }
}

void FileManagerWindow::performSearch() {
    bool ok;
    QString searchTerm = QInputDialog::getText(this, "Search", "Search for:", QLineEdit::Normal, "", &ok);
    
    if (ok && !searchTerm.isEmpty()) {
        int tabIndex = m_tabWidget->currentIndex();
        if (tabIndex < 0 || tabIndex >= m_tabData.size()) return;
        
        QDir dir(m_tabData[tabIndex].path);
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
            QMessageBox::information(this, "Search Results", 
                "Found " + QString::number(files.size()) + " files:\n\n" + results);
        }
    }
}

void FileManagerWindow::showContextMenu(const QPoint& pos) {
    QListView* fileList = qobject_cast<QListView*>(sender());
    if (!fileList) return;
    
    QModelIndex index = fileList->indexAt(pos);
    
    QMenu contextMenu(this);
    if (index.isValid()) {
        contextMenu.addAction(m_copyAction);
        contextMenu.addAction(m_cutAction);
        contextMenu.addAction(m_pasteAction);
        contextMenu.addSeparator();
        contextMenu.addAction(m_renameAction);
        contextMenu.addAction(m_deleteAction);
        contextMenu.addSeparator();
        contextMenu.addAction(m_propertiesAction);
    } else {
        contextMenu.addAction(m_newFileAction);
        contextMenu.addAction(m_newFolderAction);
        contextMenu.addSeparator();
        contextMenu.addAction(m_pasteAction);
        contextMenu.addSeparator();
        contextMenu.addAction(m_refreshAction);
        contextMenu.addAction(m_searchAction);
    }
    
    contextMenu.exec(fileList->mapToGlobal(pos));
}

void FileManagerWindow::showDirectoryContextMenu(const QPoint& pos) {
    QMenu contextMenu(this);
    contextMenu.addAction(m_newFolderAction);
    contextMenu.addSeparator();
    contextMenu.addAction(m_refreshAction);
    contextMenu.exec(mapToGlobal(pos));
}

void FileManagerWindow::updateStatusBar() {
    QListView* fileList = qobject_cast<QListView*>(m_tabWidget->currentWidget());
    if (!fileList) return;
    
    QModelIndexList selected = fileList->selectionModel()->selectedIndexes();
    
    if (selected.isEmpty()) {
        m_selectedLabel->setText("");
    } else {
        int count = selected.size();
        if (count == 1) {
            FileSortProxyModel* proxy = qobject_cast<FileSortProxyModel*>(fileList->model());
            if (proxy) {
                QModelIndex sourceIndex = proxy->mapToSource(selected.first());
                QString path = m_fileModel->filePath(sourceIndex);
                QFileInfo fi(path);
                m_selectedLabel->setText(fi.fileName());
            }
        } else {
            m_selectedLabel->setText(QString("%1 items selected").arg(count));
        }
    }
    
    // Count total items
    int tabIndex = m_tabWidget->currentIndex();
    if (tabIndex >= 0 && tabIndex < m_tabData.size()) {
        QDir dir(m_tabData[tabIndex].path);
        int total = dir.count() - 2;
        m_statusLabel->setText(QString("%1 items").arg(total));
    }
}

void FileManagerWindow::updateNavigationButtons() {
    int tabIndex = m_tabWidget->currentIndex();
    if (tabIndex < 0 || tabIndex >= m_tabHistory.size()) return;
    
    m_backAction->setEnabled(m_tabHistoryIndex[tabIndex] > 0);
    m_forwardAction->setEnabled(m_tabHistoryIndex[tabIndex] < m_tabHistory[tabIndex].size() - 1);
    
    if (tabIndex < m_tabData.size()) {
        QString currentPath = m_tabData[tabIndex].path;
        m_upAction->setEnabled(currentPath != "/" && currentPath != QDir::homePath());
    }
}

void FileManagerWindow::copyFile() {
    QListView* fileList = qobject_cast<QListView*>(m_tabWidget->currentWidget());
    if (!fileList) return;
    
    QModelIndexList selected = fileList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    
    FileSortProxyModel* proxy = qobject_cast<FileSortProxyModel*>(fileList->model());
    if (!proxy) return;
    
    m_clipboardFiles.clear();
    for (const QModelIndex& index : selected) {
        QModelIndex sourceIndex = proxy->mapToSource(index);
        m_clipboardFiles.append(m_fileModel->filePath(sourceIndex));
    }
    m_clipboardOp = ClipboardOp::Copy;
    
    statusBar()->showMessage(QString("Copied %1 file(s)").arg(m_clipboardFiles.size()), 2000);
}

void FileManagerWindow::cutFile() {
    QListView* fileList = qobject_cast<QListView*>(m_tabWidget->currentWidget());
    if (!fileList) return;
    
    QModelIndexList selected = fileList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    
    FileSortProxyModel* proxy = qobject_cast<FileSortProxyModel*>(fileList->model());
    if (!proxy) return;
    
    m_clipboardFiles.clear();
    for (const QModelIndex& index : selected) {
        QModelIndex sourceIndex = proxy->mapToSource(index);
        m_clipboardFiles.append(m_fileModel->filePath(sourceIndex));
    }
    m_clipboardOp = ClipboardOp::Cut;
    
    statusBar()->showMessage(QString("Cut %1 file(s)").arg(m_clipboardFiles.size()), 2000);
}

void FileManagerWindow::pasteFile() {
    if (m_clipboardFiles.isEmpty()) return;
    
    int tabIndex = m_tabWidget->currentIndex();
    if (tabIndex < 0 || tabIndex >= m_tabData.size()) return;
    
    for (const QString& src : m_clipboardFiles) {
        QFileInfo fi(src);
        QString dst = m_tabData[tabIndex].path + "/" + fi.fileName();
        
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
    QListView* fileList = qobject_cast<QListView*>(m_tabWidget->currentWidget());
    if (!fileList) return;
    
    QModelIndexList selected = fileList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    
    FileSortProxyModel* proxy = qobject_cast<FileSortProxyModel*>(fileList->model());
    if (!proxy) return;
    
    QStringList files;
    for (const QModelIndex& index : selected) {
        QModelIndex sourceIndex = proxy->mapToSource(index);
        files.append(m_fileModel->filePath(sourceIndex));
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
    QListView* fileList = qobject_cast<QListView*>(m_tabWidget->currentWidget());
    if (!fileList) return;
    
    QModelIndexList selected = fileList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    
    FileSortProxyModel* proxy = qobject_cast<FileSortProxyModel*>(fileList->model());
    if (!proxy) return;
    
    QModelIndex sourceIndex = proxy->mapToSource(selected.first());
    QString oldPath = m_fileModel->filePath(sourceIndex);
    QFileInfo fi(oldPath);
    
    bool ok;
    QString newName = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, fi.fileName(), &ok);
    
    if (ok && !newName.isEmpty() && newName != fi.fileName()) {
        QString newPath = fi.path() + "/" + newName;
        if (QFile::rename(oldPath, newPath)) {
            statusBar()->showMessage("Renamed to: " + newName, 2000);
            refresh();
        } else {
            QMessageBox::critical(this, "Error", "Failed to rename file");
        }
    }
}

void FileManagerWindow::properties() {
    QListView* fileList = qobject_cast<QListView*>(m_tabWidget->currentWidget());
    if (!fileList) return;
    
    QModelIndexList selected = fileList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    
    FileSortProxyModel* proxy = qobject_cast<FileSortProxyModel*>(fileList->model());
    if (!proxy) return;
    
    QModelIndex sourceIndex = proxy->mapToSource(selected.first());
    QString path = m_fileModel->filePath(sourceIndex);
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

} // namespace havel

#include "FileManager.moc"
