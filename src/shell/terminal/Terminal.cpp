// Terminal Emulator Implementation

#include "Terminal.hpp"
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QPainter>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QHBoxLayout>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace havel {

// ============================================================================
// TerminalWidget Implementation
// ============================================================================

TerminalWidget::TerminalWidget(QWidget* parent)
    : QPlainTextEdit(parent)
    , m_process(nullptr)
    , m_foreground(QColor(200, 200, 200))  // Light gray
    , m_background(QColor(30, 30, 30))      // Dark gray
    , m_cursorColor(Qt::green)
    , m_scrollbackSize(10000)
    , m_cursorBlink(true)
    , m_zoomLevel(0)
    , m_selecting(false)
{
    // Set terminal-like font
    QFont font("Monospace", 11);
    font.setStyleHint(QFont::Monospace);
    setFont(font);

    // Set colors - NO WHITE ON WHITE
    updatePalette();
    
    // Remove any borders
    setStyleSheet("QPlainTextEdit { border: none; background-color: #1e1e1e; color: #c8c8c8; }");

    // Terminal settings
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setReadOnly(false);  // Allow typing!
    setCursorBlink(m_cursorBlink);
    setMaximumBlockCount(m_scrollbackSize);
    
    // Hide scrollbars by default (can be shown)
    // verticalScrollBar()->setVisible(true);
    
    // Context menu
    setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Create process
    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyRead, this, &TerminalWidget::onReadyRead);
    connect(m_process, QOverload<QProcess::ProcessError>::of(&QProcess::errorOccurred),
            this, &TerminalWidget::onProcessError);
    connect(m_process, &QProcess::finished, this, &TerminalWidget::onProcessFinished);
}

TerminalWidget::~TerminalWidget() {
    stopProcess();
}

void TerminalWidget::startProcess(const QString& shell, const QStringList& args) {
    if (isRunning()) {
        stopProcess();
    }
    
    // Determine shell
    m_shell = shell.isEmpty() ? qgetenv("SHELL") : shell;
    if (m_shell.isEmpty()) {
        m_shell = "/bin/bash";
    }
    
    m_arguments = args.isEmpty() ? QStringList() << "-i" : args;
    
    // Set terminal environment
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TERM", "xterm-256color");
    env.insert("COLORTERM", "truecolor");
    
    m_process->setProcessEnvironment(env);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    
    // Start in home directory
    QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    m_process->setWorkingDirectory(homeDir);
    
    m_process->start(m_shell, m_arguments);
    
    if (!m_process->waitForStarted(5000)) {
        appendHtml("<font color='red'>Failed to start shell: " + m_shell + "</font>");
        return;
    }
    
    m_title = m_shell.mid(m_shell.lastIndexOf('/') + 1);
    emit titleChanged(m_title);
}

void TerminalWidget::stopProcess() {
    if (m_process && isRunning()) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void TerminalWidget::setFont(const QFont& font) {
    QPlainTextEdit::setFont(font);
    updatePalette();
}

void TerminalWidget::setColors(const QColor& fg, const QColor& bg) {
    m_foreground = fg;
    m_background = bg;
    updatePalette();
}

void TerminalWidget::setCursorBlink(bool blink) {
    m_cursorBlink = blink;
    // Cursor blink is handled by Qt's built-in cursor
}

void TerminalWidget::setScrollbackSize(int lines) {
    m_scrollbackSize = lines;
    setMaximumBlockCount(lines);
}

void TerminalWidget::setOpacity(int opacity) {
    // Set window opacity (0-100%)
    if (opacity < 5) opacity = 5;
    if (opacity > 100) opacity = 100;
    
    QWidget* pw = QWidget::parentWidget();
    if (pw) {
        pw->setWindowOpacity(opacity / 100.0);
    }
}

void TerminalWidget::copySelection() {
    if (textCursor().hasSelection()) {
        QApplication::clipboard()->setText(textCursor().selectedText());
    }
}

void TerminalWidget::pasteClipboard() {
    QString text = QApplication::clipboard()->text();
    if (!text.isEmpty() && isRunning()) {
        // Paste to process
        m_process->write(text.toLocal8Bit());
    }
}

void TerminalWidget::selectAll() {
    QPlainTextEdit::selectAll();
}

void TerminalWidget::zoomIn() {
    m_zoomLevel++;
    QFont font = this->font();
    font.setPointSize(font.pointSize() + 1);
    setFont(font);
}

void TerminalWidget::zoomOut() {
    if (m_zoomLevel > 0) {
        m_zoomLevel--;
        QFont font = this->font();
        font.setPointSize(font.pointSize() - 1);
        setFont(font);
    }
}

void TerminalWidget::resetZoom() {
    m_zoomLevel = 0;
    QFont font("Monospace", 11);
    font.setStyleHint(QFont::Monospace);
    setFont(font);
}

void TerminalWidget::flash() {
    // Visual bell - flash the background briefly
    m_flashing = true;
    setStyleSheet("QPlainTextEdit { border: none; background-color: #3a3a3a; color: #c8c8c8; }");
    
    // Reset after 100ms
    QTimer::singleShot(100, this, [this]() {
        m_flashing = false;
        updatePalette();
    });
}

void TerminalWidget::keyPressEvent(QKeyEvent* event) {
    // First check for Ctrl shortcuts
    if (event->modifiers() & Qt::ControlModifier) {
        switch (event->key()) {
            case Qt::Key_C:
                if (textCursor().hasSelection()) {
                    copySelection();
                    return;
                }
                break;
            case Qt::Key_V:
                pasteClipboard();
                return;
            case Qt::Key_Equal:
            case Qt::Key_Plus:
                zoomIn();
                return;
            case Qt::Key_Minus:
                zoomOut();
                return;
            case Qt::Key_0:
                resetZoom();
                return;
        }
    }

    // Shift for scrollback
    if (event->modifiers() & Qt::ShiftModifier) {
        switch (event->key()) {
            case Qt::Key_PageUp:
                verticalScrollBar()->triggerAction(QScrollBar::SliderPageStepSub);
                return;
            case Qt::Key_PageDown:
                verticalScrollBar()->triggerAction(QScrollBar::SliderPageStepAdd);
                return;
        }
    }

    // Send text to shell - FIX: Actually allow typing!
    if (isRunning()) {
        // Handle special keys first
        QByteArray escape = handleSpecialKey(event->key(), event->modifiers());
        if (!escape.isEmpty()) {
            m_process->write(escape);
            return;
        }
        
        // Send regular text
        QString text = event->text();
        if (!text.isEmpty()) {
            m_process->write(text.toLocal8Bit());
        } else if (event->key() < 128 && event->key() > 0) {
            // Handle ASCII keys
            m_process->write(QByteArray(1, (char)event->key()));
        }
    }
    
    // Call base class for cursor movement etc.
    QPlainTextEdit::keyPressEvent(event);
}

QByteArray TerminalWidget::handleSpecialKey(int key, Qt::KeyboardModifiers modifiers) {
    QByteArray escape;

    switch (key) {
        case Qt::Key_Up:
            escape = "\x1b[A";
            break;
        case Qt::Key_Down:
            escape = "\x1b[B";
            break;
        case Qt::Key_Right:
            escape = "\x1b[C";
            break;
        case Qt::Key_Left:
            escape = "\x1b[D";
            break;
        case Qt::Key_Home:
            escape = "\x1b[H";
            break;
        case Qt::Key_End:
            escape = "\x1b[F";
            break;
        case Qt::Key_PageUp:
            escape = "\x1b[5~";
            break;
        case Qt::Key_PageDown:
            escape = "\x1b[6~";
            break;
        case Qt::Key_Delete:
            escape = "\x1b[3~";
            break;
        case Qt::Key_Insert:
            escape = "\x1b[2~";
            break;
        case Qt::Key_F1:
            escape = "\x1bOP";
            break;
        case Qt::Key_F2:
            escape = "\x1bOQ";
            break;
        case Qt::Key_F3:
            escape = "\x1bOR";
            break;
        case Qt::Key_F4:
            escape = "\x1bOS";
            break;
        case Qt::Key_F5:
            escape = "\x1b[15~";
            break;
        case Qt::Key_F6:
            escape = "\x1b[17~";
            break;
        case Qt::Key_F7:
            escape = "\x1b[18~";
            break;
        case Qt::Key_F8:
            escape = "\x1b[19~";
            break;
        case Qt::Key_F9:
            escape = "\x1b[20~";
            break;
        case Qt::Key_F10:
            escape = "\x1b[21~";
            break;
        case Qt::Key_F11:
            escape = "\x1b[23~";
            break;
        case Qt::Key_F12:
            escape = "\x1b[24~";
            break;
        case Qt::Key_Tab:
            escape = "\t";
            break;
        case Qt::Key_Backtab:
            escape = "\x1b[Z";
            break;
        case Qt::Key_Backspace:
            escape = "\x7f";
            break;
        case Qt::Key_Escape:
            escape = "\x1b";
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            escape = "\r";
            break;
        default:
            break;
    }
    
    return escape;
}

void TerminalWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    
    QAction* copyAction = menu.addAction("Copy");
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, &TerminalWidget::copySelection);
    
    QAction* pasteAction = menu.addAction("Paste");
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, &TerminalWidget::pasteClipboard);
    
    menu.addSeparator();
    
    QAction* selectAllAction = menu.addAction("Select All");
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, this, &TerminalWidget::selectAll);
    
    menu.addSeparator();
    
    QAction* clearAction = menu.addAction("Clear");
    connect(clearAction, &QAction::triggered, [this]() {
        clear();
    });
    
    menu.exec(event->globalPos());
}

void TerminalWidget::focusInEvent(QFocusEvent* event) {
    QPlainTextEdit::focusInEvent(event);
    updatePalette();
}

void TerminalWidget::focusOutEvent(QFocusEvent* event) {
    QPlainTextEdit::focusOutEvent(event);
    updatePalette();
}

void TerminalWidget::mousePressEvent(QMouseEvent* event) {
    QPlainTextEdit::mousePressEvent(event);
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent* event) {
    QPlainTextEdit::mouseReleaseEvent(event);
}

void TerminalWidget::onReadyRead() {
    if (m_process) {
        QByteArray data = m_process->readAll();
        
        // Handle UTF-8 encoding for emoji and special characters
        QString text = QString::fromUtf8(data);
        
        // Handle basic escape sequences
        text.replace("\r\n", "\n");
        text.replace("\r", "\n");
        
        // Handle clear screen
        if (text.contains("\x1b[2J")) {
            clear();
            text.remove("\x1b[2J");
        }
        
        // Handle cursor movement (simplified)
        text.remove(QRegularExpression("\x1b\\[\\d+([A-DfH])"));
        text.remove(QRegularExpression("\x1b\\[\\d+;\\d+H"));
        
        // Handle color codes (simplified - just remove them)
        text.remove(QRegularExpression("\x1b\\[\\d+(;\\d+)*m"));
        
        if (!text.isEmpty()) {
            moveCursor(QTextCursor::End);
            insertPlainText(text);
            ensureCursorVisible();
        }
    }
}

void TerminalWidget::onProcessError(QProcess::ProcessError error) {
    QString errorMsg;
    switch (error) {
        case QProcess::FailedToStart:
            errorMsg = "Failed to start process";
            break;
        case QProcess::Crashed:
            errorMsg = "Process crashed";
            break;
        case QProcess::Timedout:
            errorMsg = "Process timed out";
            break;
        default:
            errorMsg = "Unknown error";
    }
    
    appendHtml("<font color='red'>" + errorMsg + "</font>");
}

void TerminalWidget::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    QString msg;
    if (status == QProcess::NormalExit) {
        msg = QString("\nProcess exited with code %1").arg(exitCode);
    } else {
        msg = "\nProcess crashed";
    }
    
    appendHtml("<font color='yellow'>" + msg + "</font>");
    emit processExited(exitCode);
}

void TerminalWidget::updatePalette() {
    QPalette pal = palette();
    pal.setColor(QPalette::Base, m_background);
    pal.setColor(QPalette::Text, m_foreground);
    setPalette(pal);
    
    // Set cursor color
    setCursorWidth(2);
}

void TerminalWidget::writeCommand(const QString& cmd) {
    if (isRunning()) {
        m_process->write((cmd + "\n").toLocal8Bit());
    }
}

// ============================================================================
// TerminalWindow Implementation
// ============================================================================

TerminalWindow::TerminalWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_tabWidget(nullptr)
    , m_statusBar(nullptr)
    , m_statusLabel(nullptr)
    , m_locationLabel(nullptr)
    , m_fontCombo(nullptr)
    , m_fontSizeSpin(nullptr)
    , m_opacitySpin(nullptr)
    , m_nextTabId(1)
    , m_foreground(Qt::white)
    , m_background(Qt::black)
    , m_opacity(100)
    , m_showMenuBar(true)
{
    setWindowTitle("Terminal - Havel WM");
    setMinimumSize(800, 600);
    
    // Get default shell
    m_defaultShell = qgetenv("SHELL");
    if (m_defaultShell.isEmpty()) {
        m_defaultShell = "/bin/bash";
    }
    
    setupUI();
    setupActions();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupShortcuts();
    
    loadSettings();
    
    // Create first tab
    newTab();
}

TerminalWindow::~TerminalWindow() {
    saveSettings();
}

void TerminalWindow::setupUI() {
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);
    
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &TerminalWindow::onTabChanged);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &TerminalWindow::onTabCloseRequested);
    
    setCentralWidget(m_tabWidget);
}

void TerminalWindow::setupActions() {
    m_newTabAction = new QAction(QIcon::fromTheme("tab-new"), "&New Tab", this);
    m_newTabAction->setShortcut(QKeySequence::AddTab);
    connect(m_newTabAction, &QAction::triggered, this, &TerminalWindow::onNewTab);
    
    m_closeTabAction = new QAction(QIcon::fromTheme("window-close"), "&Close Tab", this);
    m_closeTabAction->setShortcut(QKeySequence::Close);
    connect(m_closeTabAction, &QAction::triggered, this, &TerminalWindow::onCloseTab);
    
    m_renameTabAction = new QAction("Rename Tab", this);
    m_renameTabAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_T);
    connect(m_renameTabAction, &QAction::triggered, this, &TerminalWindow::onRenameTab);
    
    m_copyAction = new QAction(QIcon::fromTheme("edit-copy"), "&Copy", this);
    m_copyAction->setShortcut(QKeySequence::Copy);
    connect(m_copyAction, &QAction::triggered, this, &TerminalWindow::onCopy);
    
    m_pasteAction = new QAction(QIcon::fromTheme("edit-paste"), "&Paste", this);
    m_pasteAction->setShortcut(QKeySequence::Paste);
    connect(m_pasteAction, &QAction::triggered, this, &TerminalWindow::onPaste);
    
    m_selectAllAction = new QAction("Select &All", this);
    m_selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(m_selectAllAction, &QAction::triggered, this, &TerminalWindow::onSelectAll);

    m_zoomInAction = new QAction("Zoom &In", this);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(m_zoomInAction, &QAction::triggered, this, &TerminalWindow::onZoomIn);
    
    m_zoomOutAction = new QAction("Zoom &Out", this);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered, this, &TerminalWindow::onZoomOut);
    
    m_resetZoomAction = new QAction("&Reset Zoom", this);
    m_resetZoomAction->setShortcut(Qt::CTRL | Qt::Key_0);
    connect(m_resetZoomAction, &QAction::triggered, this, &TerminalWindow::onResetZoom);
    
    m_clearAction = new QAction("&Clear", this);
    m_clearAction->setShortcut(Qt::CTRL | Qt::Key_L);
    connect(m_clearAction, &QAction::triggered, this, &TerminalWindow::onClear);
    
    m_resetAction = new QAction("&Reset Terminal", this);
    connect(m_resetAction, &QAction::triggered, this, &TerminalWindow::onReset);
    
    m_fullscreenAction = new QAction("&Fullscreen", this);
    m_fullscreenAction->setShortcut(Qt::Key_F11);
    m_fullscreenAction->setCheckable(true);
    connect(m_fullscreenAction, &QAction::triggered, this, &TerminalWindow::onToggleFullscreen);
    
    m_menuBarAction = new QAction("Show &Menu Bar", this);
    m_menuBarAction->setCheckable(true);
    m_menuBarAction->setChecked(true);
    connect(m_menuBarAction, &QAction::triggered, this, &TerminalWindow::onToggleMenuBar);
}

void TerminalWindow::setupMenuBar() {
    m_fileMenu = menuBar()->addMenu("&File");
    m_fileMenu->addAction(m_newTabAction);
    m_fileMenu->addAction(m_closeTabAction);
    m_fileMenu->addAction(m_renameTabAction);
    m_fileMenu->addSeparator();
    
    m_editMenu = menuBar()->addMenu("&Edit");
    m_editMenu->addAction(m_copyAction);
    m_editMenu->addAction(m_pasteAction);
    m_editMenu->addAction(m_selectAllAction);
    m_editMenu->addSeparator();
    m_editMenu->addAction(m_clearAction);
    m_editMenu->addAction(m_resetAction);
    
    m_viewMenu = menuBar()->addMenu("&View");
    m_viewMenu->addAction(m_zoomInAction);
    m_viewMenu->addAction(m_zoomOutAction);
    m_viewMenu->addAction(m_resetZoomAction);
    m_viewMenu->addSeparator();
    
    // Opacity submenu
    QMenu* opacityMenu = m_viewMenu->addMenu("&Opacity");
    QSlider* opacitySlider = new QSlider(Qt::Horizontal);
    opacitySlider->setRange(5, 100);
    opacitySlider->setValue(m_opacity);
    opacitySlider->setTickPosition(QSlider::TicksBelow);
    opacitySlider->setTickInterval(10);
    QWidget* opacityWidget = new QWidget();
    QHBoxLayout* opacityLayout = new QHBoxLayout(opacityWidget);
    opacityLayout->addWidget(new QLabel("Opacity:"));
    opacityLayout->addWidget(opacitySlider);
    opacityWidget->setMinimumWidth(200);
    QWidgetAction* opacityAction = new QWidgetAction(this);
    opacityAction->setDefaultWidget(opacityWidget);
    opacityMenu->addAction(opacityAction);
    connect(opacitySlider, &QSlider::valueChanged, this, &TerminalWindow::onChangeOpacityValue);
    
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_fullscreenAction);
    m_viewMenu->addAction(m_menuBarAction);
    
    m_helpMenu = menuBar()->addMenu("&Help");
    m_helpMenu->addAction("&About", this, &TerminalWindow::onAbout);
    m_helpMenu->addAction("&Keyboard Shortcuts", this, &TerminalWindow::onShortcuts);
}

void TerminalWindow::setupToolBar() {
    QToolBar* toolBar = addToolBar("Main");
    toolBar->addAction(m_newTabAction);
    toolBar->addAction(m_closeTabAction);
    toolBar->addSeparator();
    toolBar->addAction(m_copyAction);
    toolBar->addAction(m_pasteAction);
    toolBar->addSeparator();
    toolBar->addAction(m_zoomInAction);
    toolBar->addAction(m_zoomOutAction);
    toolBar->addAction(m_resetZoomAction);
}

void TerminalWindow::setupStatusBar() {
    m_statusBar = statusBar();
    
    m_statusLabel = new QLabel("Ready");
    m_locationLabel = new QLabel("");
    
    m_statusBar->addWidget(m_statusLabel);
    m_statusBar->addPermanentWidget(m_locationLabel);
}

void TerminalWindow::setupShortcuts() {
    // Ctrl+Shift+N for new tab
    QShortcut* shortcut = new QShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_N, this);
    connect(shortcut, &QShortcut::activated, this, &TerminalWindow::onNewTab);
}

int TerminalWindow::newTab(const QString& shell, const QStringList& args) {
    TerminalWidget* terminal = new TerminalWidget(this);
    
    connect(terminal, &TerminalWidget::titleChanged, this, &TerminalWindow::updateTitle);
    connect(terminal, &TerminalWidget::tabSwitchRequested, this, &TerminalWindow::onTabSwitchRequested);
    
    int tabIndex = m_tabWidget->addTab(terminal, "Terminal");
    
    TerminalTab tab;
    tab.widget = terminal;
    tab.title = "Terminal";
    tab.id = m_nextTabId++;
    tab.active = true;
    m_tabs.append(tab);
    
    m_tabWidget->setCurrentIndex(tabIndex);
    
    // Start shell
    terminal->startProcess(shell.isEmpty() ? m_defaultShell : shell, args);
    
    updateStatusBar();
    
    return tabIndex;
}

void TerminalWindow::closeTab(int index) {
    if (m_tabs.size() <= 1) {
        // Don't close last tab, just exit
        close();
        return;
    }
    
    TerminalWidget* terminal = qobject_cast<TerminalWidget*>(m_tabWidget->widget(index));
    if (terminal) {
        terminal->stopProcess();
    }
    
    m_tabWidget->removeTab(index);
    m_tabs.removeAt(index);
    
    if (index < m_tabWidget->count()) {
        m_tabWidget->setCurrentIndex(index);
    }
    
    updateTitle();
    updateStatusBar();
}

void TerminalWindow::setCurrentTab(int index) {
    m_tabWidget->setCurrentIndex(index);
}

TerminalWidget* TerminalWindow::currentTerminal() const {
    return qobject_cast<TerminalWidget*>(m_tabWidget->currentWidget());
}

void TerminalWindow::onTabChanged(int index) {
    updateTitle();
    updateStatusBar();
}

void TerminalWindow::onTabCloseRequested(int index) {
    closeTab(index);
}

void TerminalWindow::onNewTab() {
    newTab();
}

void TerminalWindow::onCloseTab() {
    closeTab(m_tabWidget->currentIndex());
}

void TerminalWindow::onRenameTab() {
    bool ok;
    QString name = QInputDialog::getText(this, "Rename Tab", "Tab name:", 
                                          QLineEdit::Normal, "", &ok);
    if (ok && !name.isEmpty()) {
        int index = m_tabWidget->currentIndex();
        if (index >= 0 && index < m_tabs.size()) {
            m_tabs[index].title = name;
            m_tabWidget->setTabText(index, name);
        }
    }
}

void TerminalWindow::onTabSwitchRequested(int tabIndex) {
    if (tabIndex >= 0 && tabIndex < m_tabWidget->count()) {
        m_tabWidget->setCurrentIndex(tabIndex);
    }
}

void TerminalWindow::onCopy() {
    TerminalWidget* terminal = currentTerminal();
    if (terminal) {
        terminal->copySelection();
    }
}

void TerminalWindow::onPaste() {
    TerminalWidget* terminal = currentTerminal();
    if (terminal) {
        terminal->pasteClipboard();
    }
}

void TerminalWindow::onSelectAll() {
    TerminalWidget* terminal = currentTerminal();
    if (terminal) {
        terminal->selectAll();
    }
}

void TerminalWindow::onZoomIn() {
    TerminalWidget* terminal = currentTerminal();
    if (terminal) {
        terminal->zoomIn();
    }
}

void TerminalWindow::onZoomOut() {
    TerminalWidget* terminal = currentTerminal();
    if (terminal) {
        terminal->zoomOut();
    }
}

void TerminalWindow::onResetZoom() {
    TerminalWidget* terminal = currentTerminal();
    if (terminal) {
        terminal->resetZoom();
    }
}

void TerminalWindow::onClear() {
    TerminalWidget* terminal = currentTerminal();
    if (terminal) {
        terminal->clear();
    }
}

void TerminalWindow::onReset() {
    TerminalWidget* terminal = currentTerminal();
    if (terminal) {
        terminal->stopProcess();
        terminal->startProcess(m_defaultShell);
    }
}

void TerminalWindow::onChangeFont() {
    bool ok;
    QFont font = QFontDialog::getFont(&ok, m_terminalFont, this, "Select Terminal Font");
    if (ok) {
        m_terminalFont = font;
        for (const TerminalTab& tab : m_tabs) {
            tab.widget->setFont(font);
        }
    }
}

void TerminalWindow::onChangeColors() {
    QColor fg = QColorDialog::getColor(m_foreground, this, "Select Text Color");
    if (fg.isValid()) {
        m_foreground = fg;
        for (const TerminalTab& tab : m_tabs) {
            tab.widget->setColors(fg, m_background);
        }
    }
    
    QColor bg = QColorDialog::getColor(m_background, this, "Select Background Color");
    if (bg.isValid()) {
        m_background = bg;
        for (const TerminalTab& tab : m_tabs) {
            tab.widget->setColors(m_foreground, bg);
        }
    }
}

void TerminalWindow::onChangeOpacity() {
    setWindowOpacity(m_opacity / 100.0);
}

void TerminalWindow::onChangeOpacityValue(int value) {
    m_opacity = value;
    setWindowOpacity(value / 100.0);
}

void TerminalWindow::onToggleFullscreen() {
    if (isFullScreen()) {
        showNormal();
        m_fullscreenAction->setChecked(false);
    } else {
        showFullScreen();
        m_fullscreenAction->setChecked(true);
    }
}

void TerminalWindow::onToggleMenuBar() {
    m_showMenuBar = !m_showMenuBar;
    menuBar()->setVisible(m_showMenuBar);
    m_menuBarAction->setChecked(m_showMenuBar);
}

void TerminalWindow::onAbout() {
    QMessageBox::about(this, "About Terminal",
        "Havel WM Terminal\n\n"
        "A modern terminal emulator for Havel WM.\n\n"
        "Features:\n"
        "- Multiple tabs\n"
        "- True color support\n"
        "- Configurable fonts and colors\n"
        "- Scrollback buffer\n"
        "- Copy/paste support\n"
        "- Zoom in/out");
}

void TerminalWindow::onShortcuts() {
    QMessageBox::information(this, "Keyboard Shortcuts",
        "Ctrl+Shift+T  New tab\n"
        "Ctrl+W        Close tab\n"
        "Ctrl+Shift+R  Rename tab\n"
        "Ctrl+1-9      Switch to tab 1-9\n"
        "Ctrl+C        Copy selection\n"
        "Ctrl+V        Paste\n"
        "Ctrl+Shift+A  Select all\n"
        "Ctrl++        Zoom in\n"
        "Ctrl+-        Zoom out\n"
        "Ctrl+0        Reset zoom\n"
        "Ctrl+L        Clear screen\n"
        "F11           Fullscreen\n"
        "Shift+PgUp    Scroll up\n"
        "Shift+PgDn    Scroll down");
}

void TerminalWindow::updateTitle() {
    TerminalWidget* terminal = currentTerminal();
    if (terminal) {
        QString title = terminal->getTitle();
        setWindowTitle(title + " - Terminal");
        
        int index = m_tabWidget->currentIndex();
        if (index >= 0 && index < m_tabs.size()) {
            m_tabs[index].title = title;
            m_tabWidget->setTabText(index, title);
        }
    }
}

void TerminalWindow::updateStatusBar() {
    TerminalWidget* terminal = currentTerminal();
    if (terminal) {
        m_statusLabel->setText(terminal->isRunning() ? "Running" : "Exited");
        m_locationLabel->setText(QString("PID: %1").arg(terminal->getProcessId()));
    }
}

void TerminalWindow::loadSettings() {
    QSettings settings("Havel WM", "Terminal");
    
    m_terminalFont = settings.value("font", QFont("Monospace", 11)).value<QFont>();
    m_foreground = settings.value("foreground", QColor(Qt::white)).value<QColor>();
    m_background = settings.value("background", QColor(Qt::black)).value<QColor>();
    m_opacity = settings.value("opacity", 95).toInt();  // Default 95% opacity
    m_showMenuBar = settings.value("showMenuBar", true).toBool();
    
    // Apply settings
    setWindowOpacity(m_opacity / 100.0);
    menuBar()->setVisible(m_showMenuBar);
    m_menuBarAction->setChecked(m_showMenuBar);
    
    // Apply to all tabs
    for (const TerminalTab& tab : m_tabs) {
        tab.widget->setColors(m_foreground, m_background);
        tab.widget->setFont(m_terminalFont);
    }
}

void TerminalWindow::saveSettings() {
    QSettings settings("Havel WM", "Terminal");

    settings.setValue("font", m_terminalFont);
    settings.setValue("foreground", m_foreground);
    settings.setValue("background", m_background);
    settings.setValue("opacity", m_opacity);
    settings.setValue("showMenuBar", m_showMenuBar);
}

} // namespace havel

#include "Terminal.moc"
