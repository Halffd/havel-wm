#pragma once

#include <QPushButton>
#include "IPCClient.hpp"

namespace havel {

struct WindowInfo;

/**
 * Button representing a single window in taskbar
 */
class WindowButton : public QPushButton {
    Q_OBJECT

public:
    explicit WindowButton(const WindowInfo& info, QWidget* parent = nullptr);
    
    // Update button from window info
    void updateInfo(const WindowInfo& info);
    
    // Get window ID
    quint64 windowId() const { return m_windowId; }
    
    // Set active/focused state
    void setActive(bool active);

signals:
    void clicked(quint64 windowId);
    void rightClicked(quint64 windowId);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void updateStyle();
    
    quint64 m_windowId;
    QString m_appId;
    QString m_title;
    bool m_active = false;
    bool m_urgent = false;
};

} // namespace havel
