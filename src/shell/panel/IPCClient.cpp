#include "IPCClient.hpp"

#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

namespace havel {

IPCClient::IPCClient(QObject* parent)
    : QObject(parent)
    , m_socket(new QLocalSocket(this))
{
    connect(m_socket, &QLocalSocket::connected, this, &IPCClient::onConnected);
    connect(m_socket, &QLocalSocket::disconnected, this, &IPCClient::onDisconnected);
    connect(m_socket, &QLocalSocket::readyRead, this, &IPCClient::onDataReceived);
    connect(m_socket, &QLocalSocket::errorOccurred, this, &IPCClient::onSocketError);
}

IPCClient::~IPCClient() {
    disconnect();
}

bool IPCClient::connectToSocket(const QString& socketPath) {
    m_socketPath = socketPath;
    m_socket->connectToServer(socketPath);
    return m_socket->waitForConnected(1000);
}

void IPCClient::disconnect() {
    if (m_socket->state() == QLocalSocket::ConnectedState) {
        m_socket->disconnectFromServer();
    }
    m_connected = false;
    m_windows.clear();
}

void IPCClient::requestWindows() {
    sendMessage("GET_WINDOWS");
}

void IPCClient::requestFocused() {
    sendMessage("GET_FOCUSED");
}

void IPCClient::focusWindow(quint64 id) {
    sendMessage(QString("FOCUS %1").arg(id));
}

void IPCClient::minimizeWindow(quint64 id) {
    sendMessage(QString("MINIMIZE %1").arg(id));
}

void IPCClient::restoreWindow(quint64 id) {
    sendMessage(QString("RESTORE %1").arg(id));
}

void IPCClient::sendMessage(const QString& msg) {
    if (!m_connected) return;
    
    m_socket->write(msg.toUtf8());
    m_socket->flush();
}

void IPCClient::onConnected() {
    m_connected = true;
    emit connected();
}

void IPCClient::onDisconnected() {
    m_connected = false;
    m_windows.clear();
    emit disconnected();
}

void IPCClient::onSocketError(QLocalSocket::LocalSocketError error) {
    Q_UNUSED(error);
    emit socketError(m_socket->errorString());
}

void IPCClient::onDataReceived() {
    QByteArray data = m_socket->readAll();
    m_buffer += QString::fromUtf8(data);
    
    // Process complete lines
    while (m_buffer.contains('\n')) {
        int newlinePos = m_buffer.indexOf('\n');
        QString line = m_buffer.left(newlinePos);
        m_buffer = m_buffer.mid(newlinePos + 1);
        
        parseResponse(line.trimmed());
    }
}

void IPCClient::parseResponse(const QString& response) {
    if (response.startsWith("OK ")) {
        QString data = response.mid(3);
        
        // Parse window list
        if (data.startsWith("[")) {
            m_windows.clear();
            
            // Simple JSON array parsing
            QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
            if (doc.isArray()) {
                for (const auto& value : doc.array()) {
                    if (value.isObject()) {
                        QJsonObject obj = value.toObject();
                        WindowInfo info;
                        info.id = obj["id"].toDouble();
                        info.appId = obj["appId"].toString();
                        info.title = obj["title"].toString();
                        info.workspace = obj["workspace"].toInt();
                        info.flags = obj["flags"].toInt();
                        m_windows.append(info);
                    }
                }
            }
            
            emit windowsUpdated(m_windows);
        } else {
            // Single value (focused window ID)
            bool ok;
            quint64 id = data.toULongLong(&ok);
            if (ok) {
                emit focusedChanged(id);
            }
        }
    } else if (response.startsWith("ERROR ")) {
        emit socketError(response.mid(6));
    }
}

WindowInfo IPCClient::parseWindowInfo(const QString& json) {
    WindowInfo info;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        info.id = obj["id"].toDouble();
        info.appId = obj["appId"].toString();
        info.title = obj["title"].toString();
        info.workspace = obj["workspace"].toInt();
        info.flags = obj["flags"].toInt();
    }
    return info;
}

} // namespace havel
