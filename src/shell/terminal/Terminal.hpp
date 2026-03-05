// Terminal Emulator for Havel WM

#pragma once

#include <QMainWindow>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollBar>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QFontComboBox>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QMap>
#include <QColorDialog>
#include <QSettings>
#include <QFontDialog>
#include <QInputDialog>
#include <QShortcut>
#include <QProcessEnvironment>
#include <QSlider>
#include <QWidgetAction>

namespace havel {

/**
 * Terminal widget with PTY support
 */
class TerminalWidget : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget* parent = nullptr);
    ~TerminalWidget();
    
    // Terminal control
    void startProcess(const QString& shell = "", const QStringList& args = {});
    void stopProcess();
    bool isRunning() const { return m_process && m_process->state() == QProcess::Running; }
    
    // Terminal settings
    void setFont(const QFont& font);
    void setColors(const QColor& foreground, const QColor& background);
    void setCursorBlink(bool blink);
    void setScrollbackSize(int lines);
    void setOpacity(int opacity);  // 0-100%
    
    // Terminal info
    QString getTitle() const { return m_title; }
    int getProcessId() const { return m_process ? m_process->processId() : 0; }
    
    // Copy/Paste
    void copySelection();
    void pasteClipboard();
    void selectAll();
    
    // Zoom
    void zoomIn();
    void zoomOut();
    void resetZoom();
    
signals:
    void titleChanged(const QString& title);
    void processExited(int exitCode);
    void bell();
    void tabSwitchRequested(int tabIndex);  // Ctrl+1-9

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void onReadyRead();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    void writeCommand(const QString& cmd);
    void handleSpecialKey(int key, Qt::KeyboardModifiers modifiers);
    void updatePalette();
    
    QProcess* m_process;
    QString m_shell;
    QStringList m_arguments;
    QString m_title;
    
    // Colors
    QColor m_foreground;
    QColor m_background;
    QColor m_cursorColor;
    
    // Settings
    int m_scrollbackSize;
    bool m_cursorBlink;
    int m_zoomLevel;
    
    // Selection
    bool m_selecting;
    QPoint m_selectionStart;
};

/**
 * Terminal Tab
 */
struct TerminalTab {
    TerminalWidget* widget;
    QString title;
    int id;
    bool active;
};

/**
 * Terminal Window - Main application
 */
class TerminalWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit TerminalWindow(QWidget* parent = nullptr);
    ~TerminalWindow();
    
    // Tab management
    int newTab(const QString& shell = "", const QStringList& args = {});
    void closeTab(int index);
    void setCurrentTab(int index);
    TerminalWidget* currentTerminal() const;
    
    // Settings
    void loadSettings();
    void saveSettings();
    
private slots:
    // Tab management
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void onNewTab();
    void onCloseTab();
    void onRenameTab();
    void onTabSwitchRequested(int tabIndex);  // Ctrl+1-9
    
    // Terminal operations
    void onCopy();
    void onPaste();
    void onSelectAll();
    void onZoomIn();
    void onZoomOut();
    void onResetZoom();
    void onClear();
    void onReset();
    
    // Settings
    void onChangeFont();
    void onChangeColors();
    void onChangeOpacity();
    void onChangeOpacityValue(int value);
    void onToggleFullscreen();
    void onToggleMenuBar();
    
    // Help
    void onAbout();
    void onShortcuts();

private:
    void setupUI();
    void setupActions();
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupShortcuts();
    void updateTitle();
    void updateStatusBar();
    
    QTabWidget* m_tabWidget;
    QStatusBar* m_statusBar;
    QLabel* m_statusLabel;
    QLabel* m_locationLabel;
    QComboBox* m_fontCombo;
    QSpinBox* m_fontSizeSpin;
    QSpinBox* m_opacitySpin;
    
    QMenu* m_fileMenu;
    QMenu* m_editMenu;
    QMenu* m_viewMenu;
    QMenu* m_helpMenu;
    
    // Actions
    QAction* m_newTabAction;
    QAction* m_closeTabAction;
    QAction* m_renameTabAction;
    QAction* m_copyAction;
    QAction* m_pasteAction;
    QAction* m_selectAllAction;
    QAction* m_zoomInAction;
    QAction* m_zoomOutAction;
    QAction* m_resetZoomAction;
    QAction* m_clearAction;
    QAction* m_resetAction;
    QAction* m_fullscreenAction;
    QAction* m_menuBarAction;
    
    // Terminal tabs
    QVector<TerminalTab> m_tabs;
    int m_nextTabId;
    
    // Settings
    QFont m_terminalFont;
    QColor m_foreground;
    QColor m_background;
    int m_opacity;  // 0-100%
    bool m_showMenuBar;
    
    // Default shell
    QString m_defaultShell;
};

} // namespace havel
