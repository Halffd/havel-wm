#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include "AppLauncher.hpp"

namespace havel {

/**
 * Application launcher popup window
 */
class LauncherWindow : public QDialog {
    Q_OBJECT

public:
    explicit LauncherWindow(QWidget* parent = nullptr);
    ~LauncherWindow();

    // Show launcher at cursor position
    void showAtCursor();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);
    void onAppSelected(QListWidgetItem* item);
    void onAppDoubleClicked(QListWidgetItem* item);
    void onAppsScanned();

private:
    void setupUI();
    void updateAppList(const QVector<AppEntry>& apps);
    void showFavorites();
    
    AppLauncher* m_launcher;
    QLineEdit* m_searchEdit;
    QListWidget* m_appList;
    QVBoxLayout* m_layout;
    
    bool m_showingFavorites = false;
};

} // namespace havel
