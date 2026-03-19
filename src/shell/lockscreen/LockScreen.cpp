#include "LockScreen.hpp"

#include <QApplication>
#include <QScreen>
#include <QDateTime>
#include <QVBoxLayout>
#include <QProcess>
#include <QDebug>

namespace havel {

LockScreen::LockScreen(QWidget* parent)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint)
    , m_timeLabel(new QLabel(this))
    , m_dateLabel(new QLabel(this))
    , m_passwordEdit(new QLineEdit(this))
    , m_statusLabel(new QLabel(this))
    , m_unlockButton(new QPushButton("Unlock", this))
    , m_clockTimer(new QTimer(this))
{
    setupUI();
    
    // Connect signals
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LockScreen::onPasswordEntered);
    connect(m_unlockButton, &QPushButton::clicked, this, &LockScreen::onPasswordEntered);
    connect(m_clockTimer, &QTimer::timeout, this, &LockScreen::updateClock);
    
    // Update clock every second
    m_clockTimer->start(1000);
    updateClock();
    
    // Hide initially
    hide();
}

void LockScreen::setupUI() {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_X11NetWmWindowTypeSplash);
    
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(20);
    layout->setContentsMargins(40, 40, 40, 40);
    
    // Time and date (centered, large)
    m_timeLabel->setStyleSheet(
        "color: #fff; font-size: 72px; font-weight: light;"
    );
    m_timeLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_timeLabel);
    
    m_dateLabel->setStyleSheet(
        "color: #ccc; font-size: 24px;"
    );
    m_dateLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_dateLabel);
    
    layout->addSpacing(40);
    
    // Password input
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText("Password");
    m_passwordEdit->setFixedWidth(300);
    m_passwordEdit->setStyleSheet(
        "QLineEdit { "
        "  padding: 12px; "
        "  font-size: 16px; "
        "  background: rgba(255, 255, 255, 0.1); "
        "  border: 1px solid rgba(255, 255, 255, 0.2); "
        "  border-radius: 4px; "
        "  color: #fff; "
        "}"
        "QLineEdit:focus { "
        "  border-color: rgba(255, 255, 255, 0.5); "
        "  background: rgba(255, 255, 255, 0.15); "
        "}"
    );
    m_passwordEdit->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_passwordEdit, 0, Qt::AlignCenter);
    
    // Status label
    m_statusLabel->setStyleSheet("color: #ff6464; font-size: 14px;");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->hide();
    layout->addWidget(m_statusLabel, 0, Qt::AlignCenter);
    
    // Unlock button
    m_unlockButton->setFixedWidth(150);
    m_unlockButton->setFixedHeight(40);
    m_unlockButton->setStyleSheet(
        "QPushButton { "
        "  background: rgba(255, 255, 255, 0.2); "
        "  border: none; "
        "  border-radius: 4px; "
        "  color: #fff; "
        "  font-size: 16px; "
        "} "
        "QPushButton:hover { background: rgba(255, 255, 255, 0.3); } "
        "QPushButton:pressed { background: rgba(255, 255, 255, 0.4); }"
    );
    layout->addWidget(m_unlockButton, 0, Qt::AlignCenter);
    
    // Dark semi-transparent background
    setStyleSheet("QDialog { background: rgba(0, 0, 0, 0.85); }");
    
    centerOnScreen();
}

void LockScreen::centerOnScreen() {
    QScreen* screen = QApplication::primaryScreen();
    if (screen) {
        QRect geo = screen->geometry();
        QRect dialogGeo = geometry();
        move(
            geo.x() + (geo.width() - dialogGeo.width()) / 2,
            geo.y() + (geo.height() - dialogGeo.height()) / 2
        );
    }
}

void LockScreen::lock() {
    m_locked = true;
    m_passwordEdit->clear();
    m_statusLabel->hide();
    
    // Show fullscreen
    showFullScreen();
    raise();
    activateWindow();
    
    // Focus password field
    m_passwordEdit->setFocus();
    
    emit locked();
}

void LockScreen::unlock() {
    m_locked = false;
    hide();
    emit unlocked();
}

void LockScreen::onPasswordEntered() {
    QString password = m_passwordEdit->text();
    
    if (password.isEmpty()) {
        m_statusLabel->setText("Please enter password");
        m_statusLabel->show();
        return;
    }
    
    // Authenticate using PAM via system call
    if (authenticate(password)) {
        m_passwordEdit->clear();
        unlock();
    } else {
        m_statusLabel->setText("Incorrect password");
        m_statusLabel->show();
        m_passwordEdit->clear();
        
        // Shake animation could be added here
    }
}

bool LockScreen::authenticate(const QString& password) {
    // Authenticate user password using PAM
    // Uses pamtester utility for PAM authentication
    
    if (password.isEmpty()) {
        // Empty password - check if user has no password set
        // This is a security risk and should not be allowed in production
        qWarning("[LockScreen] Empty password attempt");
        return false;
    }

    // Get current username
    const char* user = getenv("USER");
    if (!user) {
        user = getenv("LOGNAME");
    }
    if (!user) {
        qCritical("[LockScreen] Cannot determine username");
        return false;
    }

    // Use pamtester for PAM authentication
    // pamtester service name should match /etc/pam.d/ configuration
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    
    // Write password to stdin of pamtester
    process.start("pamtester", QStringList() 
                  << "-v" << "gdm-password" << user << "authenticate");
    
    if (!process.waitForStarted(3000)) {
        qCritical("[LockScreen] Failed to start pamtester: %s", 
                  process.errorString().toUtf8().constData());
        // Fallback: try checkpassword if pamtester not available
        process.start("sh", QStringList() << "-c" 
                  << QString("printf '%s' | checkpassword 2>/dev/null").arg(password));
        if (!process.waitForStarted(3000)) {
            qCritical("[LockScreen] No authentication helper available");
            return false;
        }
    }
    
    // Write password to process
    process.write(password.toUtf8());
    process.write("\n");
    process.closeWriteChannel();
    
    if (!process.waitForFinished(5000)) {
        qCritical("[LockScreen] Authentication timeout");
        process.kill();
        return false;
    }

    int exitCode = process.exitCode();
    if (exitCode == 0) {
        qInfo("[LockScreen] Authentication successful for user: %s", user);
        return true;
    } else {
        qWarning("[LockScreen] Authentication failed for user: %s (exit code: %d)", 
                 user, exitCode);
        return false;
    }
}

void LockScreen::onUnlockFailed() {
    m_statusLabel->setText("Authentication failed");
    m_statusLabel->show();
    m_passwordEdit->clear();
    m_passwordEdit->setFocus();
}

void LockScreen::updateClock() {
    QDateTime now = QDateTime::currentDateTime();
    m_timeLabel->setText(now.toString("HH:mm"));
    m_dateLabel->setText(now.toString("dddd, MMMM d, yyyy"));
}

} // namespace havel
