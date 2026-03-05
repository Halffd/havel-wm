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

namespace havel {

/**
 * File Manager Window
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
    
    // View
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
    
    // Search
    void performSearch();
    
    // Context menu
    void showContextMenu(const QPoint& pos);
    void showDirectoryContextMenu(const QPoint& pos);

private:
    void setupUI();
    void setupActions();
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupShortcuts();
    void updateStatusBar();
    void updateNavigationButtons();
    
    // File operations helpers
    void copyFileInternal(const QString& src, const QString& dst);
    bool confirmDelete(const QStringList& files);
    
    // UI components
    QTreeView* m_directoryTree;
    QListView* m_fileList;
    QFileSystemModel* m_fileModel;
    QFileSystemModel* m_directoryModel;
    
    QLineEdit* m_locationBar;
    QLabel* m_statusLabel;
    QLabel* m_selectedLabel;
    
    QToolBar* m_toolBar;
    QMenu* m_fileMenu;
    QMenu* m_editMenu;
    QMenu* m_viewMenu;
    QMenu* m_helpMenu;
    
    // Actions
    QAction* m_backAction;
    QAction* m_forwardAction;
    QAction* m_upAction;
    QAction* m_homeAction;
    QAction* m_refreshAction;
    
    QAction* m_newFileAction;
    QAction* m_newFolderAction;
    QAction* m_copyAction;
    QAction* m_cutAction;
    QAction* m_pasteAction;
    QAction* m_deleteAction;
    QAction* m_renameAction;
    QAction* m_propertiesAction;
    
    QAction* m_searchAction;
    
    // Navigation history
    QVector<QString> m_history;
    int m_historyIndex;
    
    // Clipboard operations
    enum class ClipboardOp { None, Copy, Cut };
    ClipboardOp m_clipboardOp;
    QStringList m_clipboardFiles;
    
    QString m_currentPath;
};

} // namespace havel
