#pragma once

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include "IPCClient.hpp"
#include "LauncherWindow.hpp"

namespace havel {

class WindowButton;

/**
 * Main panel window (taskbar)
 */
class PanelWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit PanelWindow(QWidget* parent = nullptr);
    ~PanelWindow();

    // Connect to compositor IPC
    bool connectToCompositor(const QString& socketPath);

private slots:
    void onWindowsUpdated(const QVector<WindowInfo>& windows);
    void onFocusedChanged(quint64 windowId);
    void onWindowButtonClicked(quint64 windowId);
    void onWindowButtonRightClicked(quint64 windowId);
    void onPinRequested(const QString& appId);
    void onUnpinRequested(const QString& appId);
    void onCloseRequested(quint64 windowId);
    void onIPCError(const QString& error);
    void onIPCConnected();
    void onIPCDisconnected();

private:
    void setupUI();
    void updateWindowList();
    WindowButton* findButton(quint64 windowId);
    void updateClock();
    void setOpacity(int opacity);
    void hidePanel();
    void showPanel();
    
    QWidget* m_centralWidget;
    QHBoxLayout* m_layout;
    QLabel* m_statusLabel;
    QPushButton* m_launcherButton;
    QLabel* m_clockLabel;
    
    IPCClient* m_ipcClient;
    LauncherWindow* m_launcher;
    QVector<WindowButton*> m_buttons;
    quint64 m_focusedWindowId = 0;
};

} // namespace havel
