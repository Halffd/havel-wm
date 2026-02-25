#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

namespace havel {

/**
 * Lock screen - password prompt to unlock session
 */
class LockScreen : public QDialog {
    Q_OBJECT

public:
    explicit LockScreen(QWidget* parent = nullptr);
    
    // Lock the screen
    void lock();
    
    // Unlock (called after successful auth)
    void unlock();
    
    // Check if currently locked
    bool isLocked() const { return m_locked; }

signals:
    void locked();
    void unlocked();

private slots:
    void onPasswordEntered();
    void onUnlockFailed();
    void updateClock();

private:
    void setupUI();
    void centerOnScreen();
    bool authenticate(const QString& password);
    
    QLabel* m_timeLabel;
    QLabel* m_dateLabel;
    QLineEdit* m_passwordEdit;
    QLabel* m_statusLabel;
    QPushButton* m_unlockButton;
    
    QTimer* m_clockTimer;
    bool m_locked = false;
};

} // namespace havel
