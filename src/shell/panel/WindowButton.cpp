#include "WindowButton.hpp"
#include "IPCClient.hpp"

#include <QMouseEvent>
#include <QMenu>
#include <QToolTip>

namespace havel {

WindowButton::WindowButton(const WindowInfo& info, QWidget* parent)
    : QPushButton(parent)
    , m_windowId(info.id)
    , m_appId(info.appId)
    , m_title(info.title)
    , m_active(info.isFocused())
    , m_urgent(info.isUrgent())
{
    updateInfo(info);
    updateStyle();
    
    setMaximumWidth(200);
    setMinimumWidth(100);
    setFixedHeight(36);
    
    // Show full title on hover
    setToolTip(info.title);
}

void WindowButton::updateInfo(const WindowInfo& info) {
    m_windowId = info.id;
    m_appId = info.appId;
    m_title = info.title;
    m_active = info.isFocused();
    m_urgent = info.isUrgent();
    
    // Truncate title if too long
    QString displayText = info.title;
    if (displayText.length() > 25) {
        displayText = displayText.left(22) + "...";
    }
    
    setText(displayText);
    updateStyle();
}

void WindowButton::setActive(bool active) {
    m_active = active;
    updateStyle();
}

void WindowButton::updateStyle() {
    QStringList styles;
    
    // Base style
    styles << "padding: 4px 8px";
    styles << "border: 1px solid #444";
    styles << "border-radius: 4px";
    styles << "background: #2a2a30";
    styles << "color: #eee";
    styles << "text-align: left";
    
    if (m_active) {
        styles << "background: #3a3a45";
        styles << "border-color: #6496ff";
    }
    
    if (m_urgent) {
        styles << "background: #4a3a3a";
        styles << "border-color: #ff6464";
    }
    
    setStyleSheet(styles.join("; "));
}

void WindowButton::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_windowId);
    }
    QPushButton::mousePressEvent(event);
}

void WindowButton::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    
    QAction* focusAction = menu.addAction(m_active ? "Unfocus" : "Focus");
    menu.addSeparator();
    QAction* minimizeAction = menu.addAction("Minimize");
    QAction* closeAction = menu.addAction("Close");
    
    connect(focusAction, &QAction::triggered, this, [this]() {
        emit clicked(m_windowId);
    });
    connect(minimizeAction, &QAction::triggered, this, [this]() {
        emit rightClicked(m_windowId);
    });
    
    // Close would need IPC support
    closeAction->setEnabled(false);
    
    menu.exec(event->globalPos());
}

} // namespace havel
