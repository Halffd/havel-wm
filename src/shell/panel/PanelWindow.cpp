#include "PanelWindow.hpp"
#include "WindowButton.hpp"

#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QDateTime>

namespace havel {

PanelWindow::PanelWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_centralWidget(new QWidget(this))
    , m_layout(new QHBoxLayout(m_centralWidget))
    , m_statusLabel(new QLabel(this))
    , m_launcherButton(new QPushButton(this))
    , m_clockLabel(new QLabel(this))
    , m_ipcClient(new IPCClient(this))
    , m_launcher(new LauncherWindow(this))
{
    setupUI();
    
    // Connect IPC signals
    connect(m_ipcClient, &IPCClient::connected, this, &PanelWindow::onIPCConnected);
    connect(m_ipcClient, &IPCClient::disconnected, this, &PanelWindow::onIPCDisconnected);
    connect(m_ipcClient, &IPCClient::socketError, this, &PanelWindow::onIPCError);
    connect(m_ipcClient, &IPCClient::windowsUpdated, this, &PanelWindow::onWindowsUpdated);
    connect(m_ipcClient, &IPCClient::focusedChanged, this, &PanelWindow::onFocusedChanged);
    
    // Launcher button
    connect(m_launcherButton, &QPushButton::clicked, this, [this]() {
        m_launcher->showAtCursor();
    });
    
    // Clock timer
    QTimer* clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, &PanelWindow::updateClock);
    clockTimer->start(1000);  // Update every second
    updateClock();
    
    // Initial window list request after connection
    QTimer::singleShot(500, this, [this]() {
        if (m_ipcClient->isConnected()) {
            m_ipcClient->requestWindows();
        }
    });
    
    // Periodic refresh
    QTimer* refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, [this]() {
        if (m_ipcClient->isConnected()) {
            m_ipcClient->requestWindows();
        }
    });
    refreshTimer->start(2000);  // Refresh every 2 seconds
}

PanelWindow::~PanelWindow() = default;

bool PanelWindow::connectToCompositor(const QString& socketPath) {
    return m_ipcClient->connectToSocket(socketPath);
}

void PanelWindow::setupUI() {
    setCentralWidget(m_centralWidget);
    
    // Window flags for panel behavior
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_X11NetWmWindowTypeDock);
    
    // Dark theme
    setStyleSheet(
        "QMainWindow { background: #1a1a20; }"
        "QWidget { background: #1a1a20; color: #eee; }"
        "QPushButton { "
        "  background: #2a2a30; "
        "  border: 1px solid #444; "
        "  border-radius: 4px; "
        "  padding: 4px 12px; "
        "  color: #eee; "
        "  font-size: 14px; "
        "} "
        "QPushButton:hover { background: #3a3a45; } "
        "QPushButton:pressed { background: #4a4a55; } "
        "QLabel { color: #aaa; padding: 4px 8px; font-size: 13px; }"
    );
    
    // Layout
    m_layout->setContentsMargins(8, 4, 8, 4);
    m_layout->setSpacing(8);
    
    // Launcher button (left side)
    m_launcherButton->setText("🚀 Apps");
    m_launcherButton->setFixedHeight(36);
    m_launcherButton->setToolTip("Click to open application launcher");
    m_layout->addWidget(m_launcherButton);
    
    // Status label (hidden by default, shown for errors)
    m_statusLabel->setText("");
    m_statusLabel->setStyleSheet("color: #ff6464;");
    m_layout->addWidget(m_statusLabel);
    
    m_layout->addStretch();
    
    // Window buttons will be added here dynamically
    
    m_layout->addStretch();
    
    // Clock (right side)
    m_clockLabel->setStyleSheet("color: #aaa; font-size: 13px; padding: 4px 8px;");
    m_layout->addWidget(m_clockLabel);
    
    // Set fixed height
    setFixedHeight(48);
    
    // Position at bottom of screen
    QScreen* screen = QApplication::primaryScreen();
    if (screen) {
        QRect geo = screen->geometry();
        setGeometry(0, geo.height() - 48, geo.width(), 48);
    }
}

void PanelWindow::updateClock() {
    QDateTime now = QDateTime::currentDateTime();
    m_clockLabel->setText(now.toString("ddd MMM d  HH:mm"));
}

void PanelWindow::onWindowsUpdated(const QVector<WindowInfo>& windows) {
    // Update existing buttons and create new ones
    QSet<quint64> visibleIds;
    
    for (const auto& info : windows) {
        visibleIds.insert(info.id);
        
        WindowButton* button = findButton(info.id);
        if (button) {
            // Update existing
            button->updateInfo(info);
        } else {
            // Create new
            button = new WindowButton(info, this);
            connect(button, &WindowButton::clicked, this, &PanelWindow::onWindowButtonClicked);
            connect(button, &WindowButton::rightClicked, this, &PanelWindow::onWindowButtonRightClicked);
            m_layout->addWidget(button);
            m_buttons.append(button);
        }
    }
    
    // Remove buttons for closed windows
    for (auto it = m_buttons.begin(); it != m_buttons.end(); ) {
        if (!visibleIds.contains((*it)->windowId())) {
            WindowButton* button = *it;
            it = m_buttons.erase(it);
            m_layout->removeWidget(button);
            button->deleteLater();
        } else {
            ++it;
        }
    }
    
    // Update focused state
    for (WindowButton* button : m_buttons) {
        button->setActive(button->windowId() == m_focusedWindowId);
    }
}

void PanelWindow::onFocusedChanged(quint64 windowId) {
    m_focusedWindowId = windowId;
    
    for (WindowButton* button : m_buttons) {
        button->setActive(button->windowId() == windowId);
    }
}

void PanelWindow::onWindowButtonClicked(quint64 windowId) {
    if (windowId == m_focusedWindowId) {
        // Already focused - minimize
        m_ipcClient->minimizeWindow(windowId);
    } else {
        // Focus window
        m_ipcClient->focusWindow(windowId);
    }
}

void PanelWindow::onWindowButtonRightClicked(quint64 windowId) {
    // Minimize on right click
    m_ipcClient->minimizeWindow(windowId);
}

void PanelWindow::onIPCError(const QString& error) {
    m_statusLabel->setText(QString("Error: %1").arg(error));
}

void PanelWindow::onIPCConnected() {
    m_statusLabel->setText("Connected");
    m_ipcClient->requestWindows();
}

void PanelWindow::onIPCDisconnected() {
    m_statusLabel->setText("Disconnected");
    m_buttons.clear();
    
    // Clear layout
    QLayoutItem* item;
    while ((item = m_layout->takeAt(1)) != nullptr) {
        delete item->widget();
    }
}

WindowButton* PanelWindow::findButton(quint64 windowId) {
    for (WindowButton* button : m_buttons) {
        if (button->windowId() == windowId) {
            return button;
        }
    }
    return nullptr;
}

} // namespace havel
