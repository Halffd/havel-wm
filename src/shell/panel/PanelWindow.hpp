#pragma once

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include "IPCClient.hpp"

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
    void onIPCError(const QString& error);
    void onIPCConnected();
    void onIPCDisconnected();

private:
    void setupUI();
    void updateWindowList();
    WindowButton* findButton(quint64 windowId);
    
    QWidget* m_centralWidget;
    QHBoxLayout* m_layout;
    QLabel* m_statusLabel;
    
    IPCClient* m_ipcClient;
    QVector<WindowButton*> m_buttons;
    quint64 m_focusedWindowId = 0;
};

} // namespace havel
