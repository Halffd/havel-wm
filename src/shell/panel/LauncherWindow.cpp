#include "LauncherWindow.hpp"

#include <QApplication>
#include <QScreen>
#include <QCursor>
#include <QKeyEvent>
#include <QListWidgetItem>
#include <QTimer>

namespace havel {

LauncherWindow::LauncherWindow(QWidget* parent)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Popup)
    , m_launcher(new AppLauncher(this))
    , m_searchEdit(new QLineEdit(this))
    , m_appList(new QListWidget(this))
    , m_layout(new QVBoxLayout(this))
{
    setupUI();
    
    // Connect signals
    connect(m_searchEdit, &QLineEdit::textEdited, this, &LauncherWindow::onSearchTextChanged);
    connect(m_appList, &QListWidget::itemClicked, this, &LauncherWindow::onAppSelected);
    connect(m_appList, &QListWidget::itemDoubleClicked, this, &LauncherWindow::onAppDoubleClicked);
    connect(m_launcher, &AppLauncher::scanComplete, this, &LauncherWindow::onAppsScanned);
    
    // Scan applications
    m_launcher->scanApplications();
    
    // Set minimum size
    setMinimumSize(400, 300);
    setMaximumWidth(600);
    
    // Dark theme
    setStyleSheet(
        "QDialog { "
        "  background: #1a1a20; "
        "  border: 1px solid #444; "
        "  border-radius: 8px; "
        "} "
        "QLineEdit { "
        "  padding: 12px; "
        "  font-size: 16px; "
        "  background: #2a2a30; "
        "  border: none; "
        "  border-radius: 4px; "
        "  color: #eee; "
        "} "
        "QListWidget { "
        "  background: transparent; "
        "  border: none; "
        "  color: #eee; "
        "  font-size: 14px; "
        "} "
        "QListWidget::item { "
        "  padding: 8px 12px; "
        "  border-radius: 4px; "
        "  margin: 2px 4px; "
        "} "
        "QListWidget::item:selected { "
        "  background: #3a3a45; "
        "} "
        "QListWidget::item:hover { "
        "  background: #2a2a35; "
        "}"
    );
}

LauncherWindow::~LauncherWindow() = default;

void LauncherWindow::setupUI() {
    m_layout->setContentsMargins(12, 12, 12, 12);
    m_layout->setSpacing(8);
    
    // Search box
    m_searchEdit->setPlaceholderText("Type to search applications...");
    m_layout->addWidget(m_searchEdit);
    
    // App list
    m_appList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_appList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_appList->setSelectionRectVisible(false);
    m_layout->addWidget(m_appList);
    
    setLayout(m_layout);
}

void LauncherWindow::showAtCursor() {
    // Position at cursor
    QPoint pos = QCursor::pos();
    QScreen* screen = QApplication::screenAt(pos);
    if (!screen) screen = QApplication::primaryScreen();
    
    QRect geo = geometry();
    int x = pos.x() - geo.width() / 2;
    int y = pos.y() - 50;
    
    // Keep on screen
    if (screen) {
        QRect screenGeo = screen->geometry();
        x = qBound(screenGeo.left(), x, screenGeo.right() - geo.width());
        y = qBound(screenGeo.top(), y, screenGeo.bottom() - geo.height());
    }
    
    move(x, y);
    
    // Show and focus
    show();
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
    
    // Show favorites initially
    showFavorites();
}

void LauncherWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    
    if (event->key() == Qt::Key_Down && m_appList->count() > 0) {
        m_appList->setCurrentRow(0);
        m_appList->setFocus();
        return;
    }
    
    QDialog::keyPressEvent(event);
}

void LauncherWindow::focusOutEvent(QFocusEvent* event) {
    // Hide when losing focus
    QTimer::singleShot(100, this, [this]() {
        if (!hasFocus() && !m_searchEdit->hasFocus() && !m_appList->hasFocus()) {
            hide();
        }
    });
    
    QDialog::focusOutEvent(event);
}

void LauncherWindow::onSearchTextChanged(const QString& text) {
    m_showingFavorites = false;
    
    if (text.isEmpty()) {
        showFavorites();
    } else {
        auto results = m_launcher->search(text);
        updateAppList(results);
    }
}

void LauncherWindow::onAppSelected(QListWidgetItem* item) {
    QString appId = item->data(Qt::UserRole).toString();
    m_launcher->launchById(appId);
    hide();
}

void LauncherWindow::onAppDoubleClicked(QListWidgetItem* item) {
    onAppSelected(item);
}

void LauncherWindow::onAppsScanned() {
    showFavorites();
}

void LauncherWindow::showFavorites() {
    m_showingFavorites = true;
    m_searchEdit->clear();
    
    auto favorites = m_launcher->favorites();
    updateAppList(favorites);
}

void LauncherWindow::updateAppList(const QVector<AppEntry>& apps) {
    m_appList->clear();
    
    for (const auto& app : apps) {
        auto* item = new QListWidgetItem();
        
        // Create item with icon and text
        if (!app.icon.isEmpty()) {
            // Try to load icon from theme
            QIcon icon = QIcon::fromTheme(app.icon);
            if (!icon.isNull()) {
                item->setIcon(icon);
            }
        }
        
        // Use default icon if none found
        if (item->icon().isNull()) {
            item->setIcon(QIcon::fromTheme("application-x-executable"));
        }
        
        // Set text
        QString text = app.name;
        if (!app.comment.isEmpty() && !m_showingFavorites) {
            text += QString("\n<small>%1</small>").arg(app.comment);
        }
        item->setText(text);
        
        // Store app ID
        item->setData(Qt::UserRole, app.id);
        
        m_appList->addItem(item);
    }
    
    // Select first item
    if (m_appList->count() > 0) {
        m_appList->setCurrentRow(0);
    }
}

} // namespace havel
