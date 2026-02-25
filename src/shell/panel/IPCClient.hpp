#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QLocalSocket>

namespace havel {

struct WindowInfo {
    quint64 id;
    QString appId;
    QString title;
    quint32 workspace;
    quint32 flags;
    
    bool isFocused() const { return flags & 1; }
    bool isUrgent() const { return flags & 2; }
    bool isMinimized() const { return flags & 4; }
    bool isFloating() const { return flags & 8; }
    bool isFullscreen() const { return flags & 16; }
    bool isMaximized() const { return flags & 32; }
};

/**
 * IPC client for communicating with compositor
 */
class IPCClient : public QObject {
    Q_OBJECT

public:
    explicit IPCClient(QObject* parent = nullptr);
    ~IPCClient();

    // Connect to compositor IPC socket
    bool connectToSocket(const QString& socketPath);
    void disconnect();
    bool isConnected() const { return m_connected; }

    // Query window data
    void requestWindows();
    void requestFocused();
    
    // Window actions
    void focusWindow(quint64 id);
    void minimizeWindow(quint64 id);
    void restoreWindow(quint64 id);

    // Get current window list
    const QVector<WindowInfo>& windows() const { return m_windows; }

signals:
    void connected();
    void disconnected();
    void windowsUpdated(const QVector<WindowInfo>& windows);
    void focusedChanged(quint64 windowId);
    void socketError(const QString& message);

private slots:
    void onDataReceived();
    void onConnected();
    void onDisconnected();
    void onSocketError(QLocalSocket::LocalSocketError error);

private:
    void sendMessage(const QString& msg);
    void parseResponse(const QString& response);
    WindowInfo parseWindowInfo(const QString& json);

    QLocalSocket* m_socket;
    QString m_socketPath;
    bool m_connected = false;
    QVector<WindowInfo> m_windows;
    QString m_buffer;
};

} // namespace havel
