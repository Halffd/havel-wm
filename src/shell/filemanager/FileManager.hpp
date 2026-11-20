// File Manager - Qt-based file browser for Havel WM

#include <QMainWindow>
#include <QTreeView>
#include <QListView>
#include <QFileSystemModel>
#include <QToolBar>
#include <QStatusBar>
#include <QMenuBar>
#include <QSplitter>
#include <QLineEdit>
#include <QLabel>
#include <QAction>
#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QProgressDialog>
#include <QShortcut>
#include <QKeyEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QClipboard>
#include <QApplication>
#include <QTabWidget>
#include <QComboBox>
#include <QButtonGroup>
#include <QRadioButton>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSortFilterProxyModel>
#include <QHeaderView>
#include <QCompleter>
#include <QDesktopServices>
#include <QMimeDatabase>
#include <QMimeType>
#include <QIcon>
#include <QKeySequence>
#include <QDateTime>
#include <QDirIterator>
#include <QScrollArea>
#include <QGraphicsView>
#include <QGraphicsScene>

namespace havel {

/**
 * Sort modes for file ordering
 */
enum class SortMode {
    Name,
    Size,
    Type,
    DateModified,
    DateCreated,
    Extension
};

/**
 * Sort order
 */
enum class SortOrder {
    Ascending,
    Descending
};

/**
 * Grouping modes for file grouping
 */
enum class GroupMode {
    None,
    Type,
    Date,
    Size,
    Extension
};

/**
 * Custom proxy model for sorting and filtering
 */
class FileSortProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit FileSortProxyModel(QObject* parent = nullptr);
    
    void setSortMode(SortMode mode);
    void setGroupMode(GroupMode mode);
    void setCustomSortOrder(SortOrder order);
    
    // Override sorting
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
    
    // Grouping support
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

private:
    SortMode m_sortMode;
    SortOrder m_customOrder;
    GroupMode m_groupMode;
    
    // Helper functions
    QString getFileExtension(const QString& fileName) const;
    QString getFileType(const QString& filePath) const;
    int compareFiles(const QModelIndex& left, const QModelIndex& right) const;
};

/**
 * Tab data for file manager tabs
 */
struct TabData {
    QString path;
    QString historyPath;  // For tab-specific history
};

/**
 * File Manager Window with tabs
 */
class FileManagerWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit FileManagerWindow(const QString& startPath = "", QWidget* parent = nullptr);
    ~FileManagerWindow();

private slots:
    // Navigation
    void navigateUp();
    void navigateHome();
    void navigateBack();
    void navigateForward();
    void navigateToPath(const QString& path);
    
    // Tab management
    void newTab(const QString& path = "");
    void closeTab(int index);
    void currentTabChanged(int index);
    void duplicateTab();
    
    // View operations
    void onDirectoryClicked(const QModelIndex& index);
    void onFileDoubleClicked(const QModelIndex& index);
    void onFileClicked(const QModelIndex& index);
    void updateLocationBar(const QModelIndex& index);
    
    // File operations
    void newFile();
    void newFolder();
    void copyFile();
    void cutFile();
    void pasteFile();
    void deleteFile();
    void renameFile();
    void properties();
    void refresh();
    
    // Sorting and grouping
    void setSortMode(SortMode mode);
    void toggleSortOrder();
    void setGroupMode(GroupMode mode);
    void updateSortMenu();
    void updateGroupMenu();
    
    // Search
    void performSearch();
    void filterFiles(const QString& text);
    
    // Context menu
    void showContextMenu(const QPoint& pos);
    void showDirectoryContextMenu(const QPoint& pos);
    
    // View mode
    void setViewMode(int mode);  // 0=icons, 1=list, 2=details
    void toggleImagePreview();
    void updateImagePreview(const QString& filePath);
    QString formatFileSize(qint64 size);

private:
    void setupUI();
    void setupActions();
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupShortcuts();
    void updateStatusBar();
    void updateNavigationButtons();
    void createFileView(int tabIndex);
    void createImagePreviewPanel();
    
    // File operations helpers
    void copyFileInternal(const QString& src, const QString& dst);
    bool confirmDelete(const QStringList& files);
    
    // UI components
    QTabWidget* m_tabWidget;
    QToolBar* m_mainToolBar;
    QToolBar* m_viewToolBar;
    
    QLineEdit* m_locationBar;
    QLineEdit* m_searchBar;
    QLabel* m_statusLabel;
    QLabel* m_selectedLabel;
    
    QComboBox* m_sortComboBox;
    QComboBox* m_groupComboBox;
    
    QMenu* m_fileMenu;
    QMenu* m_editMenu;
    QMenu* m_viewMenu;
    QMenu* m_sortMenu;
    QMenu* m_groupMenu;
    QMenu* m_helpMenu;
    
    // Actions
    QAction* m_backAction;
    QAction* m_forwardAction;
    QAction* m_upAction;
    QAction* m_homeAction;
    QAction* m_refreshAction;
    
    QAction* m_newTabAction;
    QAction* m_closeTabAction;
    QAction* m_duplicateTabAction;
    
    QAction* m_newFileAction;
    QAction* m_newFolderAction;
    QAction* m_copyAction;
    QAction* m_cutAction;
    QAction* m_pasteAction;
    QAction* m_deleteAction;
    QAction* m_renameAction;
    QAction* m_propertiesAction;
    
    QAction* m_searchAction;
    
    QAction* m_viewIconsAction;
    QAction* m_viewListAction;
    QAction* m_viewDetailsAction;
    
    // Sort actions
    QAction* m_sortByNameAction;
    QAction* m_sortBySizeAction;
    QAction* m_sortByTypeAction;
    QAction* m_sortByDateAction;
    QAction* m_sortAscendingAction;
    QAction* m_sortDescendingAction;
    
    // Group actions
    QAction* m_groupNoneAction;
    QAction* m_groupByTypeAction;
    QAction* m_groupByDateAction;
    QAction* m_groupBySizeAction;
    
    // Models
    QFileSystemModel* m_fileModel;
    QFileSystemModel* m_directoryModel;
    QVector<FileSortProxyModel*> m_proxyModels;
    
    // Tab data
    QVector<TabData> m_tabData;
    
    // Navigation history (per-tab)
    QVector<QVector<QString>> m_tabHistory;
    QVector<int> m_tabHistoryIndex;
    
    // Clipboard operations
    enum class ClipboardOp { None, Copy, Cut };
    ClipboardOp m_clipboardOp;
    QStringList m_clipboardFiles;
    
    // Current state
    SortMode m_currentSortMode;
    SortOrder m_currentSortOrder;
    GroupMode m_currentGroupMode;
    int m_currentViewMode;  // 0=icons, 1=list, 2=details

    // Image preview panel
    QWidget* m_imagePreviewPanel;
    QLabel* m_imagePreviewLabel;
    QLabel* m_imageInfoLabel;
    bool m_showImagePreview;

    // Thumbnail generator
    class ThumbnailGenerator* m_thumbnailGenerator;
};

} // namespace havel
