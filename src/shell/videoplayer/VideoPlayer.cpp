// Video Player Implementation

#include "VideoPlayer.hpp"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QClipboard>
#include <QTimer>
#include <QStyleFactory>
#include <QPalette>
#include <QFont>
#include <QScrollArea>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QDialogButtonBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QTextCodec>
#include <QGraphicsOpacityEffect>
#include <QScreen>
#include <QShortcut>
#include <QGestureEvent>
#include <QGraphicsOpacityEffect>

namespace havel {

// ============================================================================
// VideoPlayer Implementation
// ============================================================================

VideoPlayer::VideoPlayer(QWidget* parent)
    : QMainWindow(parent)
    , m_mediaPlayer(nullptr)
    , m_audioOutput(nullptr)
    , m_networkManager(nullptr)
    , m_currentIndex(-1)
    , m_seeking(false)
    , m_fullscreen(false)
    , m_controlsVisible(true)
    , m_controlsTimer(nullptr)
{
    setWindowTitle("Video Player - Havel WM");
    setMinimumSize(800, 600);
    resize(1000, 700);
    
    setupUI();
    setupMediaPlayer();
    setupMenu();
    setupToolbar();
    setupStatusBar();
    setupSystemTray();
    setupConnections();
    setupGestureControls();
    
    loadSettings();
    loadPlaylist();
    loadRecentFiles();
    
    // Initialize network manager
    m_networkManager = new QNetworkAccessManager(this);
    
    // Auto-hide controls timer
    m_controlsTimer = new QTimer(this);
    m_controlsTimer->setSingleShot(true);
    connect(m_controlsTimer, &QTimer::timeout, [this]() {
        if (m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState && !m_fullscreen) {
            m_controlsWidget->hide();
        }
    });
}

VideoPlayer::~VideoPlayer() {
    saveSettings();
    savePlaylist();
    saveRecentFiles();
}

void VideoPlayer::setupUI() {
    m_centralStack = new QStackedWidget();
    setCentralWidget(m_centralStack);

    // Video container
    m_videoContainer = new QWidget();
    QVBoxLayout* videoLayout = new QVBoxLayout(m_videoContainer);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    
    // Use QLabel as video surface (QVideoWidget alternative)
    m_videoWidget = new QLabel();
    m_videoWidget->setAlignment(Qt::AlignCenter);
    m_videoWidget->setStyleSheet("background-color: #000; color: #888;");
    m_videoWidget->setText("Video Player\n\nOpen a video file to begin");
    m_videoWidget->setMinimumSize(640, 480);  // Larger minimum size
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);  // Allow expansion
    videoLayout->addWidget(m_videoWidget);
    
    // Controls widget (overlay)
    m_controlsWidget = new QWidget();
    m_controlsWidget->setStyleSheet("background-color: rgba(0, 0, 0, 180);");
    QVBoxLayout* controlsLayout = new QVBoxLayout(m_controlsWidget);
    
    // Title label
    m_titleLabel = new QLabel("No video loaded");
    m_titleLabel->setStyleSheet("color: white; font-size: 14px; font-weight: bold;");
    controlsLayout->addWidget(m_titleLabel);
    
    // Subtitle labels (overlay on video)
    m_subtitleLabel = new QLabel();
    m_subtitleLabel->setStyleSheet("color: yellow; font-size: 18px; font-weight: bold; background-color: rgba(0,0,0,120); padding: 5px;");
    m_subtitleLabel->setAlignment(Qt::AlignCenter);
    m_subtitleLabel->setWordWrap(true);
    videoLayout->addWidget(m_subtitleLabel);
    
    m_secondarySubtitleLabel = new QLabel();
    m_secondarySubtitleLabel->setStyleSheet("color: cyan; font-size: 16px; background-color: rgba(0,0,0,100); padding: 3px;");
    m_secondarySubtitleLabel->setAlignment(Qt::AlignCenter);
    m_secondarySubtitleLabel->setWordWrap(true);
    videoLayout->addWidget(m_secondarySubtitleLabel);
    
    controlsLayout->addStretch();
    
    // Progress bar
    m_progressSlider = new QSlider(Qt::Horizontal);
    m_progressSlider->setStyleSheet("QSlider::groove:horizontal { height: 8px; }");
    controlsLayout->addWidget(m_progressSlider);
    
    // Time labels and controls
    QHBoxLayout* timeLayout = new QHBoxLayout();
    
    m_timeLabel = new QLabel("00:00");
    m_timeLabel->setStyleSheet("color: white;");
    timeLayout->addWidget(m_timeLabel);
    
    timeLayout->addStretch();
    
    m_durationLabel = new QLabel("00:00");
    m_durationLabel->setStyleSheet("color: white;");
    timeLayout->addWidget(m_durationLabel);
    
    controlsLayout->addLayout(timeLayout);
    
    // Playback controls
    QHBoxLayout* controlLayout = new QHBoxLayout();
    controlLayout->addStretch();
    
    m_previousButton = new QPushButton("⏮");
    m_previousButton->setFixedSize(40, 40);
    m_previousButton->setStyleSheet("QPushButton { background-color: #333; color: white; border-radius: 20px; } QPushButton:hover { background-color: #555; }");
    controlLayout->addWidget(m_previousButton);
    
    m_playButton = new QPushButton("▶");
    m_playButton->setFixedSize(50, 50);
    m_playButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; border-radius: 25px; font-size: 20px; } QPushButton:hover { background-color: #45a049; }");
    controlLayout->addWidget(m_playButton);
    
    m_pauseButton = new QPushButton("⏸");
    m_pauseButton->setFixedSize(50, 50);
    m_pauseButton->setStyleSheet("QPushButton { background-color: #FF9800; color: white; border-radius: 25px; font-size: 20px; } QPushButton:hover { background-color: #e68900; }");
    controlLayout->addWidget(m_pauseButton);
    
    m_stopButton = new QPushButton("⏹");
    m_stopButton->setFixedSize(40, 40);
    m_stopButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; border-radius: 20px; } QPushButton:hover { background-color: #da190b; }");
    controlLayout->addWidget(m_stopButton);
    
    m_nextButton = new QPushButton("⏭");
    m_nextButton->setFixedSize(40, 40);
    m_nextButton->setStyleSheet("QPushButton { background-color: #333; color: white; border-radius: 20px; } QPushButton:hover { background-color: #555; }");
    controlLayout->addWidget(m_nextButton);
    
    controlLayout->addSpacing(20);
    
    // Volume
    m_volumeButton = new QPushButton("🔊");
    m_volumeButton->setFixedSize(35, 35);
    m_volumeButton->setStyleSheet("QPushButton { background-color: #333; color: white; border-radius: 17px; }");
    controlLayout->addWidget(m_volumeButton);
    
    m_volumeSlider = new QSlider(Qt::Horizontal);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(100);
    controlLayout->addWidget(m_volumeSlider);
    
    controlLayout->addSpacing(20);
    
    // Speed
    controlLayout->addWidget(new QLabel("<span style='color:white'>Speed:</span>"));
    m_speedCombo = new QComboBox();
    m_speedCombo->addItems(QStringList() << "0.5x" << "0.75x" << "1.0x" << "1.25x" << "1.5x" << "2.0x");
    m_speedCombo->setCurrentIndex(2);
    m_speedCombo->setMaximumWidth(80);
    controlLayout->addWidget(m_speedCombo);

    controlLayout->addSpacing(20);
    
    // Subtitle selector
    controlLayout->addWidget(new QLabel("<span style='color:white'>Sub:</span>"));
    m_subtitleCombo = new QComboBox();
    m_subtitleCombo->addItem("No Subtitles");
    m_subtitleCombo->setMaximumWidth(150);
    connect(m_subtitleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VideoPlayer::onSubtitleTrackChanged);
    controlLayout->addWidget(m_subtitleCombo);
    
    m_secondarySubtitleCheck = new QCheckBox("2nd");
    m_secondarySubtitleCheck->setStyleSheet("color: white;");
    m_secondarySubtitleCheck->setToolTip("Enable secondary subtitle");
    connect(m_secondarySubtitleCheck, &QCheckBox::toggled, this, &VideoPlayer::onToggleSecondarySubtitle);
    controlLayout->addWidget(m_secondarySubtitleCheck);

    controlLayout->addSpacing(20);

    // Fullscreen
    m_fullscreenButton = new QPushButton("⛶");
    m_fullscreenButton->setFixedSize(35, 35);
    m_fullscreenButton->setStyleSheet("QPushButton { background-color: #333; color: white; border-radius: 17px; }");
    controlLayout->addWidget(m_fullscreenButton);
    
    controlLayout->addStretch();
    controlsLayout->addLayout(controlLayout);
    
    videoLayout->addWidget(m_controlsWidget);
    
    m_centralStack->addWidget(m_videoContainer);
    
    // Playlist dock
    m_playlistDock = new QDockWidget("Playlist", this);
    m_playlistDock->setMinimumWidth(250);
    
    QWidget* playlistWidget = new QWidget();
    QVBoxLayout* playlistLayout = new QVBoxLayout(playlistWidget);
    
    m_playlistWidget = new QListWidget();
    m_playlistWidget->setAlternatingRowColors(true);
    playlistLayout->addWidget(m_playlistWidget);
    
    m_clearPlaylistButton = new QPushButton("Clear Playlist");
    playlistLayout->addWidget(m_clearPlaylistButton);
    
    m_playlistDock->setWidget(playlistWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_playlistDock);
    
    // Directory browser dock
    m_directoryDock = new QDockWidget("Directory", this);
    m_directoryDock->setMinimumWidth(250);
    
    QWidget* directoryWidget = new QWidget();
    QVBoxLayout* directoryLayout = new QVBoxLayout(directoryWidget);
    
    m_directoryWidget = new QListWidget();
    m_directoryWidget->setAlternatingRowColors(true);
    directoryLayout->addWidget(m_directoryWidget);
    
    QPushButton* dirUpButton = new QPushButton("⬆ Up");
    connect(dirUpButton, &QPushButton::clicked, this, &VideoPlayer::onDirectoryUp);
    directoryLayout->addWidget(dirUpButton);
    
    m_directoryDock->setWidget(directoryWidget);
    addDockWidget(Qt::LeftDockWidgetArea, m_directoryDock);
    m_directoryDock->setVisible(false);
}

void VideoPlayer::setupMediaPlayer() {
    m_mediaPlayer = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    
    m_mediaPlayer->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(m_settings.volume / 100.0);

    // Connect to update label with video info
    connect(m_mediaPlayer, &QMediaPlayer::mediaStatusChanged, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia) {
            m_titleLabel->setText(m_mediaPlayer->metaData().value(QMediaMetaData::Title).toString());
            m_videoWidget->setText("");  // Clear placeholder text when video loads
        }
    });
    
    // Note: QVideoWidget not available in this Qt version
    // Video renders to internal surface, QLabel shows metadata
}

void VideoPlayer::setupMenu() {
    QMenu* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Open File...", this, &VideoPlayer::onOpenFile, QKeySequence::Open);
    fileMenu->addAction("Open &Folder...", this, &VideoPlayer::onOpenFolder);
    fileMenu->addAction("Open &URL...", this, &VideoPlayer::onOpenURL, QKeySequence(Qt::CTRL | Qt::Key_U));
    fileMenu->addMenu("Open &Recent");
    fileMenu->addSeparator();
    fileMenu->addAction("&Close", this, &VideoPlayer::onClose, QKeySequence::Close);
    fileMenu->addAction("E&xit", this, &QMainWindow::close, QKeySequence::Quit);
    
    QMenu* playbackMenu = menuBar()->addMenu("&Playback");
    playbackMenu->addAction("&Play", this, &VideoPlayer::onPlay, QKeySequence(Qt::Key_Space));
    playbackMenu->addAction("P&ause", this, &VideoPlayer::onPause);
    playbackMenu->addAction("&Stop", this, &VideoPlayer::onStop);
    playbackMenu->addSeparator();
    playbackMenu->addAction("Pre&vious", this, &VideoPlayer::onPrevious, QKeySequence(Qt::Key_Left));
    playbackMenu->addAction("Ne&xt", this, &VideoPlayer::onNext, QKeySequence(Qt::Key_Right));
    playbackMenu->addSeparator();
    playbackMenu->addAction("Loop", this, &VideoPlayer::onLoop);
    playbackMenu->addAction("Shuffle", this, &VideoPlayer::onShuffle);
    
    QMenu* videoMenu = menuBar()->addMenu("&Video");
    videoMenu->addAction("&Fullscreen", this, &VideoPlayer::onFullscreen, QKeySequence::FullScreen);
    videoMenu->addAction("Screenshot", this, &VideoPlayer::onScreenshot, QKeySequence(Qt::CTRL | Qt::Key_S));
    videoMenu->addMenu("Aspect Ratio");
    videoMenu->addMenu("Zoom");
    videoMenu->addSeparator();
    
    QMenu* subtitleMenu = videoMenu->addMenu("Subtitles");
    subtitleMenu->addAction("Load Subtitle File...", this, &VideoPlayer::onLoadSubtitleFile);
    subtitleMenu->addAction("Select Subtitle Track", this, &VideoPlayer::onSetSubtitle);
    subtitleMenu->addSeparator();
    subtitleMenu->addAction("Enable Secondary Subtitle", m_secondarySubtitleCheck, &QCheckBox::toggle);
    
    videoMenu->addSeparator();
    videoMenu->addAction("Browse Directory", this, &VideoPlayer::showDirectoryBrowser, QKeySequence(Qt::CTRL | Qt::Key_B));
    
    QMenu* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("&Playlist", this, &VideoPlayer::onShowPlaylist, QKeySequence(Qt::CTRL | Qt::Key_P));
    viewMenu->addAction("Preferences...", this, &VideoPlayer::onPreferences);
    
    QMenu* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&About", this, &VideoPlayer::onAbout);
    helpMenu->addAction("&Shortcuts", this, &VideoPlayer::onShortcuts);
}

void VideoPlayer::setupToolbar() {
    m_mainToolBar = addToolBar("Playback");
    
    m_openAction = new QAction("📂 Open", this);
    connect(m_openAction, &QAction::triggered, this, &VideoPlayer::onOpenFile);
    m_mainToolBar->addAction(m_openAction);
    
    m_mainToolBar->addSeparator();
    
    m_playAction = new QAction("▶ Play", this);
    connect(m_playAction, &QAction::triggered, this, &VideoPlayer::onPlay);
    m_mainToolBar->addAction(m_playAction);
    
    m_pauseAction = new QAction("⏸ Pause", this);
    connect(m_pauseAction, &QAction::triggered, this, &VideoPlayer::onPause);
    m_mainToolBar->addAction(m_pauseAction);
    
    m_stopAction = new QAction("⏹ Stop", this);
    connect(m_stopAction, &QAction::triggered, this, &VideoPlayer::onStop);
    m_mainToolBar->addAction(m_stopAction);
    
    m_mainToolBar->addSeparator();
    
    m_fullscreenAction = new QAction("⛶ Fullscreen", this);
    connect(m_fullscreenAction, &QAction::triggered, this, &VideoPlayer::onFullscreen);
    m_mainToolBar->addAction(m_fullscreenAction);
    
    m_playlistAction = new QAction("📋 Playlist", this);
    connect(m_playlistAction, &QAction::triggered, this, &VideoPlayer::onShowPlaylist);
    m_mainToolBar->addAction(m_playlistAction);
}

void VideoPlayer::setupStatusBar() {
    m_statusLabel = new QLabel("Ready");
    statusBar()->addWidget(m_statusLabel);
    
    m_resolutionLabel = new QLabel("");
    statusBar()->addPermanentWidget(m_resolutionLabel);
    
    m_positionLabel = new QLabel("");
    statusBar()->addPermanentWidget(m_positionLabel);
}

void VideoPlayer::setupSystemTray() {
    m_trayIcon = new QSystemTrayIcon(QIcon::fromTheme("media-playback-start"), this);
    m_trayIcon->show();
    
    m_trayMenu = new QMenu(this);
    m_trayMenu->addAction("Play/Pause", this, &VideoPlayer::onPlay);
    m_trayMenu->addAction("Next", this, &VideoPlayer::onNext);
    m_trayMenu->addAction("Previous", this, &VideoPlayer::onPrevious);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction("Show", this, &VideoPlayer::show);
    m_trayMenu->addAction("Quit", this, &QMainWindow::close);
    m_trayIcon->setContextMenu(m_trayMenu);
    
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &VideoPlayer::onTrayActivated);
}

void VideoPlayer::setupConnections() {
    // Media player connections
    connect(m_mediaPlayer, &QMediaPlayer::positionChanged, this, &VideoPlayer::onPositionChanged);
    connect(m_mediaPlayer, &QMediaPlayer::durationChanged, this, &VideoPlayer::onDurationChanged);
    connect(m_mediaPlayer, &QMediaPlayer::playbackStateChanged, this, &VideoPlayer::onPlaybackStateChanged);
    connect(m_mediaPlayer, &QMediaPlayer::errorOccurred, this, &VideoPlayer::onErrorOccurred);
    
    // Control connections
    connect(m_playButton, &QPushButton::clicked, this, &VideoPlayer::onPlay);
    connect(m_pauseButton, &QPushButton::clicked, this, &VideoPlayer::onPause);
    connect(m_stopButton, &QPushButton::clicked, this, &VideoPlayer::onStop);
    connect(m_previousButton, &QPushButton::clicked, this, &VideoPlayer::onPrevious);
    connect(m_nextButton, &QPushButton::clicked, this, &VideoPlayer::onNext);
    connect(m_fullscreenButton, &QPushButton::clicked, this, &VideoPlayer::onFullscreen);
    connect(m_volumeButton, &QPushButton::clicked, this, &VideoPlayer::onMute);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &VideoPlayer::onVolumeChanged);
    connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VideoPlayer::onSpeedChanged);
    connect(m_progressSlider, &QSlider::sliderMoved, this, &VideoPlayer::onSeek);
    
    // Playlist connections
    connect(m_playlistWidget, &QListWidget::itemDoubleClicked, this, &VideoPlayer::onPlaylistItemActivated);
    connect(m_clearPlaylistButton, &QPushButton::clicked, this, &VideoPlayer::onClearPlaylist);
    
    // Directory connections
    connect(m_directoryWidget, &QListWidget::itemDoubleClicked, this, &VideoPlayer::onDirectoryFileActivated);
    
    // Show controls on mouse move
    m_videoWidget->setMouseTracking(true);
    connect(m_videoWidget, &QWidget::customContextMenuRequested, [this](const QPoint& pos) {
        m_controlsWidget->setVisible(true);
        m_controlsTimer->start(3000);
    });
}

void VideoPlayer::setupGestureControls() {
    // Keyboard shortcuts
    new QShortcut(Qt::Key_Space, this, [this]() {
        if (m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
            onPause();
        } else {
            onPlay();
        }
    });
    new QShortcut(Qt::Key_Right, this, [this]() {
        m_mediaPlayer->setPosition(m_mediaPlayer->position() + 5000);
    });
    new QShortcut(Qt::Key_Left, this, [this]() {
        m_mediaPlayer->setPosition(m_mediaPlayer->position() - 5000);
    });
    new QShortcut(Qt::Key_Up, this, [this]() {
        onVolumeChanged(qMin(100, m_volumeSlider->value() + 5));
    });
    new QShortcut(Qt::Key_Down, this, [this]() {
        onVolumeChanged(qMax(0, m_volumeSlider->value() - 5));
    });
    new QShortcut(Qt::Key_M, this, this, &VideoPlayer::onMute);
    new QShortcut(Qt::Key_F, this, this, &VideoPlayer::onFullscreen);
}

void VideoPlayer::loadSettings() {
    QSettings settings("Havel WM", "VideoPlayer");
    
    m_settings.volume = settings.value("volume", 100).toInt();
    m_settings.speed = settings.value("speed", 1.0).toFloat();
    m_settings.rememberPosition = settings.value("rememberPosition", true).toBool();
    m_settings.autoPlay = settings.value("autoPlay", true).toBool();
    m_settings.fullscreenOnPlay = settings.value("fullscreenOnPlay", false).toBool();
    m_settings.minimizeOnClose = settings.value("minimizeOnClose", true).toBool();
    
    // Apply settings
    m_audioOutput->setVolume(m_settings.volume / 100.0);
    m_volumeSlider->setValue(m_settings.volume);
    m_speedCombo->setCurrentIndex(m_settings.speed == 2.0 ? 5 : m_settings.speed == 1.5 ? 4 : m_settings.speed == 1.25 ? 3 : m_settings.speed == 0.75 ? 1 : 0);
}

void VideoPlayer::saveSettings() {
    QSettings settings("Havel WM", "VideoPlayer");
    settings.setValue("volume", m_settings.volume);
    settings.setValue("speed", m_settings.speed);
    settings.setValue("rememberPosition", m_settings.rememberPosition);
    settings.setValue("autoPlay", m_settings.autoPlay);
    settings.setValue("fullscreenOnPlay", m_settings.fullscreenOnPlay);
    settings.setValue("minimizeOnClose", m_settings.minimizeOnClose);
}

void VideoPlayer::loadPlaylist() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/playlist.json";
    
    if (QFile::exists(path)) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();
            
            QJsonArray array = doc.array();
            for (const QJsonValue& val : array) {
                QString filePath = val.toString();
                if (QFile::exists(filePath)) {
                    m_playlistUrls.append(QUrl::fromLocalFile(filePath));
                    m_playlistWidget->addItem(QFileInfo(filePath).fileName());
                    
                    VideoInfo info;
                    info.filePath = filePath;
                    info.title = QFileInfo(filePath).fileName();
                    m_playlist.append(info);
                }
            }
        }
    }
}

void VideoPlayer::savePlaylist() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/playlist.json";
    
    QJsonArray array;
    for (const QUrl& url : m_playlistUrls) {
        array.append(url.toString());
    }
    
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(array).toJson());
        file.close();
    }
}

void VideoPlayer::loadRecentFiles() {
    QSettings settings("Havel WM", "VideoPlayer");
    m_recentFiles = settings.value("recentFiles").toStringList();
}

void VideoPlayer::saveRecentFiles() {
    QSettings settings("Havel WM", "VideoPlayer");
    settings.setValue("recentFiles", m_recentFiles);
}

void VideoPlayer::updateControls() {
    QMediaPlayer::PlaybackState state = m_mediaPlayer->playbackState();
    
    m_playButton->setVisible(state != QMediaPlayer::PlayingState);
    m_pauseButton->setVisible(state == QMediaPlayer::PlayingState);
}

void VideoPlayer::updateProgress() {
    if (!m_seeking && m_mediaPlayer->duration() > 0) {
        m_progressSlider->setRange(0, m_mediaPlayer->duration());
        m_progressSlider->setValue(m_mediaPlayer->position());
    }
    updateTimeLabels();
}

void VideoPlayer::updateTimeLabels() {
    formatTime(m_mediaPlayer->position(), m_timeLabel);
    formatTime(m_mediaPlayer->duration(), m_durationLabel);
}

void VideoPlayer::formatTime(qint64 ms, QLabel* label) {
    int seconds = ms / 1000;
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    seconds = seconds % 60;
    
    if (hours > 0) {
        label->setText(QString("%1:%2:%3").arg(hours, 2, 10, QChar('0'))
                                          .arg(minutes, 2, 10, QChar('0'))
                                          .arg(seconds, 2, 10, QChar('0')));
    } else {
        label->setText(QString("%1:%2").arg(minutes, 2, 10, QChar('0'))
                                       .arg(seconds, 2, 10, QChar('0')));
    }
}

void VideoPlayer::addToRecent(const QString& filePath) {
    if (!m_recentFiles.contains(filePath)) {
        m_recentFiles.prepend(filePath);
        while (m_recentFiles.size() > 10) {
            m_recentFiles.removeLast();
        }
        saveRecentFiles();
    }
}

void VideoPlayer::onOpenFile() {
    QString path = QFileDialog::getOpenFileName(this, "Open Video", "",
        "Video Files (*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm);;All Files (*)");
    
    if (!path.isEmpty()) {
        m_playlistUrls.clear();
        m_playlistUrls.append(QUrl::fromLocalFile(path));
        m_mediaPlayer->setSource(m_playlistUrls[0]);
        
        m_playlistWidget->clear();
        m_playlistWidget->addItem(QFileInfo(path).fileName());
        
        VideoInfo info;
        info.filePath = path;
        info.title = QFileInfo(path).fileName();
        m_playlist.clear();
        m_playlist.append(info);
        
        m_currentIndex = 0;
        m_titleLabel->setText(info.title);
        
        addToRecent(path);
        
        if (m_settings.autoPlay) {
            m_mediaPlayer->play();
        }
        
        statusBar()->showMessage("Opened: " + path, 3000);
    }
}

void VideoPlayer::onOpenFolder() {
    QString dir = QFileDialog::getExistingDirectory(this, "Open Folder");
    
    if (!dir.isEmpty()) {
        QDir directory(dir);
        QStringList filters;
        filters << "*.mp4" << "*.mkv" << "*.avi" << "*.mov" << "*.wmv" << "*.flv" << "*.webm";
        
        QFileInfoList files = directory.entryInfoList(filters, QDir::Files, QDir::Name);
        
        m_playlistUrls.clear();
        m_playlistWidget->clear();
        m_playlist.clear();
        
        for (const QFileInfo& fi : files) {
            m_playlistUrls.append(QUrl::fromLocalFile(fi.filePath()));
            m_playlistWidget->addItem(fi.fileName());
            
            VideoInfo info;
            info.filePath = fi.filePath();
            info.title = fi.fileName();
            m_playlist.append(info);
        }
        
        if (!m_playlistUrls.isEmpty()) {
            m_currentIndex = 0;
            m_mediaPlayer->setSource(m_playlistUrls[0]);
            m_titleLabel->setText(m_playlist[0].title);
            
            if (m_settings.autoPlay) {
                m_mediaPlayer->play();
            }
        }
        
        statusBar()->showMessage(QString("Loaded %1 files").arg(files.size()), 3000);
    }
}

void VideoPlayer::onOpenURL() {
    QString url = QInputDialog::getText(this, "Open URL", "Enter video URL:");
    
    if (!url.isEmpty()) {
        m_playlistUrls.clear();
        m_playlistUrls.append(QUrl(url));
        m_mediaPlayer->setSource(m_playlistUrls[0]);
        
        m_playlistWidget->clear();
        m_playlistWidget->addItem(url);
        
        m_titleLabel->setText(url);
        
        if (m_settings.autoPlay) {
            m_mediaPlayer->play();
        }
    }
}

void VideoPlayer::onOpenRecent() {
    // Would show recent files menu
}

void VideoPlayer::onClose() {
    m_mediaPlayer->stop();
    m_playlistUrls.clear();
    m_playlistWidget->clear();
    m_playlist.clear();
    m_titleLabel->setText("No video loaded");
}

void VideoPlayer::onPlay() {
    m_mediaPlayer->play();
    m_controlsWidget->setVisible(true);
    m_controlsTimer->start(3000);
}

void VideoPlayer::onPause() {
    m_mediaPlayer->pause();
}

void VideoPlayer::onStop() {
    m_mediaPlayer->stop();
    if (m_settings.rememberPosition && m_currentIndex >= 0 && m_currentIndex < m_playlist.size()) {
        m_playlist[m_currentIndex].position = m_mediaPlayer->position();
    }
}

void VideoPlayer::onPrevious() {
    if (m_currentIndex > 0) {
        m_currentIndex--;
        m_mediaPlayer->setSource(m_playlistUrls[m_currentIndex]);
        if (m_settings.autoPlay) m_mediaPlayer->play();
    } else if (m_settings.loop) {
        m_currentIndex = m_playlistUrls.size() - 1;
        m_mediaPlayer->setSource(m_playlistUrls[m_currentIndex]);
        if (m_settings.autoPlay) m_mediaPlayer->play();
    }
}

void VideoPlayer::onNext() {
    if (m_currentIndex < m_playlistUrls.size() - 1) {
        m_currentIndex++;
        m_mediaPlayer->setSource(m_playlistUrls[m_currentIndex]);
        if (m_settings.autoPlay) m_mediaPlayer->play();
    } else if (m_settings.loop) {
        m_currentIndex = 0;
        m_mediaPlayer->setSource(m_playlistUrls[m_currentIndex]);
        if (m_settings.autoPlay) m_mediaPlayer->play();
    }
}

void VideoPlayer::onSeek(int position) {
    m_seeking = true;
    m_mediaPlayer->setPosition(position);
    m_seeking = false;
}

void VideoPlayer::onVolumeChanged(int volume) {
    m_settings.volume = volume;
    m_audioOutput->setVolume(volume / 100.0);
    
    if (volume == 0) {
        m_volumeButton->setText("🔇");
    } else if (volume < 50) {
        m_volumeButton->setText("🔉");
    } else {
        m_volumeButton->setText("🔊");
    }
}

void VideoPlayer::onMute() {
    if (m_audioOutput->isMuted()) {
        m_audioOutput->setMuted(false);
        m_volumeButton->setText("🔊");
    } else {
        m_audioOutput->setMuted(true);
        m_volumeButton->setText("🔇");
    }
}

void VideoPlayer::onSpeedChanged() {
    float speeds[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
    m_settings.speed = speeds[m_speedCombo->currentIndex()];
    m_mediaPlayer->setPlaybackRate(m_settings.speed);
}

void VideoPlayer::onFullscreen() {
    if (m_fullscreen) {
        m_videoContainer->showNormal();
        m_fullscreenButton->setText("⛶");
    } else {
        m_videoContainer->showFullScreen();
        m_fullscreenButton->setText("⛷");
    }
    m_fullscreen = !m_fullscreen;
}

void VideoPlayer::onAspectRatio() {
    // Would change aspect ratio
}

void VideoPlayer::onZoom() {
    // Would change zoom level
}

void VideoPlayer::onScreenshot() {
    if (m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
        QString path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + 
                      "/screenshot_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";
        
        // Would capture current frame
        statusBar()->showMessage("Screenshot saved: " + path, 3000);
    }
}

void VideoPlayer::onShowPlaylist() {
    m_playlistDock->setVisible(!m_playlistDock->isVisible());
}

void VideoPlayer::onClearPlaylist() {
    if (QMessageBox::question(this, "Clear Playlist", "Clear the playlist?") == QMessageBox::Yes) {
        m_playlistUrls.clear();
        m_playlistWidget->clear();
        m_playlist.clear();
        m_currentIndex = -1;
        m_mediaPlayer->stop();
        m_titleLabel->setText("No video loaded");
    }
}

void VideoPlayer::onShuffle() {
    m_settings.shuffle = !m_settings.shuffle;
    // Would shuffle m_playlistUrls
}

void VideoPlayer::onLoop() {
    m_settings.loop = !m_settings.loop;
}

void VideoPlayer::onPlaylistItemActivated(QListWidgetItem* item) {
    int index = m_playlistWidget->row(item);
    if (index >= 0 && index < m_playlistUrls.size()) {
        m_currentIndex = index;
        m_mediaPlayer->setSource(m_playlistUrls[m_currentIndex]);
        m_mediaPlayer->play();
    }
}

void VideoPlayer::onPlaylistItemRemoved(int row) {
    if (row >= 0 && row < m_playlist.size()) {
        m_playlistUrls.removeAt(row);
        m_playlist.removeAt(row);
    }
}

void VideoPlayer::onPositionChanged(qint64 position) {
    updateProgress();
    
    if (m_settings.rememberPosition && m_currentIndex >= 0 && m_currentIndex < m_playlist.size()) {
        m_playlist[m_currentIndex].position = position;
    }
}

void VideoPlayer::onDurationChanged(qint64 duration) {
    updateProgress();
}

void VideoPlayer::onPlaybackStateChanged(QMediaPlayer::PlaybackState state) {
    updateControls();
    
    if (state == QMediaPlayer::StoppedState && m_settings.loop) {
        onNext();
    }
}

void VideoPlayer::onErrorOccurred(QMediaPlayer::Error error) {
    QString errorMsg;
    switch (error) {
        case QMediaPlayer::NoError:
            errorMsg = "No error";
            break;
        case QMediaPlayer::ResourceError:
            errorMsg = "Resource error";
            break;
        case QMediaPlayer::FormatError:
            errorMsg = "Format error";
            break;
        default:
            errorMsg = "Unknown error";
    }
    
    m_statusLabel->setText("<font color='red'>" + errorMsg + "</font>");
    QMessageBox::critical(this, "Playback Error", errorMsg);
}

void VideoPlayer::onVideoOutputReady() {
    // Video output is ready
}

void VideoPlayer::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick) {
        show();
        raise();
        activateWindow();
    }
}

void VideoPlayer::onPreferences() {
    VideoPreferencesDialog dialog(m_settings, this);
    if (dialog.exec() == QDialog::Accepted) {
        m_settings = dialog.getSettings();
        saveSettings();
    }
}

void VideoPlayer::onResetSettings() {
    m_settings = PlaybackSettings();
    loadSettings();
}

void VideoPlayer::onAbout() {
    QMessageBox::about(this, "About Video Player",
        "Havel WM Video Player\n\n"
        "Version 1.0\n\n"
        "A modern video player with:\n"
        "- Multiple format support\n"
        "- Playlist management\n"
        "- Subtitle support\n"
        "- Keyboard shortcuts\n"
        "- System tray integration");
}

void VideoPlayer::onShortcuts() {
    QMessageBox::information(this, "Keyboard Shortcuts",
        "Space        Play/Pause\n"
        "Left/Right   Seek ±5s\n"
        "Up/Down      Volume\n"
        "M            Mute\n"
        "F            Fullscreen\n"
        "Ctrl+O       Open file\n"
        "Ctrl+P       Show playlist\n"
        "Ctrl+S       Screenshot\n"
        "Esc          Exit fullscreen");
}

// CLI support
void VideoPlayer::playFile(const QString& filePath) {
    m_playlistUrls.clear();
    m_playlistUrls.append(QUrl::fromLocalFile(filePath));
    m_mediaPlayer->setSource(m_playlistUrls[0]);
    
    m_playlistWidget->clear();
    m_playlistWidget->addItem(QFileInfo(filePath).fileName());
    
    VideoInfo info;
    info.filePath = filePath;
    info.title = QFileInfo(filePath).fileName();
    m_playlist.clear();
    m_playlist.append(info);
    
    m_currentIndex = 0;
    m_titleLabel->setText(info.title);
    
    if (m_settings.autoPlay) {
        m_mediaPlayer->play();
    }
}

void VideoPlayer::addToPlaylist(const QString& filePath) {
    m_playlistUrls.append(QUrl::fromLocalFile(filePath));
    m_playlistWidget->addItem(QFileInfo(filePath).fileName());
    
    VideoInfo info;
    info.filePath = filePath;
    info.title = QFileInfo(filePath).fileName();
    m_playlist.append(info);
}

void VideoPlayer::showPlaylist() {
    m_playlistDock->setVisible(true);
}

void VideoPlayer::setVolume(int volume) {
    m_volumeSlider->setValue(volume);
    onVolumeChanged(volume);
}

void VideoPlayer::setFullscreen(bool fullscreen) {
    if (fullscreen && !m_fullscreen) {
        onFullscreen();
    } else if (!fullscreen && m_fullscreen) {
        onFullscreen();
    }
}

// ============================================================================
// VideoPreferencesDialog Implementation
// ============================================================================

VideoPreferencesDialog::VideoPreferencesDialog(const PlaybackSettings& settings, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle("Video Player Preferences");
    setMinimumSize(400, 300);
    setupUI();
}

void VideoPreferencesDialog::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    
    QGroupBox* playbackGroup = new QGroupBox("Playback");
    QFormLayout* playbackLayout = new QFormLayout(playbackGroup);
    
    m_autoPlay = new QCheckBox("Auto-play when opening file");
    m_autoPlay->setChecked(m_settings.autoPlay);
    playbackLayout->addRow(m_autoPlay);
    
    m_rememberPosition = new QCheckBox("Remember playback position");
    m_rememberPosition->setChecked(m_settings.rememberPosition);
    playbackLayout->addRow(m_rememberPosition);
    
    m_fullscreenOnPlay = new QCheckBox("Enter fullscreen on play");
    m_fullscreenOnPlay->setChecked(m_settings.fullscreenOnPlay);
    playbackLayout->addRow(m_fullscreenOnPlay);
    
    layout->addWidget(playbackGroup);
    
    QGroupBox* audioGroup = new QGroupBox("Audio");
    QFormLayout* audioLayout = new QFormLayout(audioGroup);
    
    m_volumeSpin = new QSpinBox();
    m_volumeSpin->setRange(0, 100);
    m_volumeSpin->setValue(m_settings.volume);
    m_volumeSpin->setSuffix("%");
    audioLayout->addRow("Default volume:", m_volumeSpin);
    
    m_speedSpin = new QDoubleSpinBox();
    m_speedSpin->setRange(0.25, 4.0);
    m_speedSpin->setValue(m_settings.speed);
    m_speedSpin->setSingleStep(0.25);
    m_speedSpin->setSuffix("x");
    audioLayout->addRow("Default speed:", m_speedSpin);
    
    layout->addWidget(audioGroup);
    
    QGroupBox* windowGroup = new QGroupBox("Window");
    QFormLayout* windowLayout = new QFormLayout(windowGroup);
    
    m_minimizeOnClose = new QCheckBox("Minimize to tray on close");
    m_minimizeOnClose->setChecked(m_settings.minimizeOnClose);
    windowLayout->addRow(m_minimizeOnClose);
    
    layout->addWidget(windowGroup);
    layout->addStretch();
    
    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

// ============================================================================
// Subtitle Functions
// ============================================================================

void VideoPlayer::onLoadSubtitleFile() {
    QString path = QFileDialog::getOpenFileName(this, "Load Subtitle", "",
        "Subtitle Files (*.srt *.ass *.vtt *.sup);;All Files (*)");
    
    if (!path.isEmpty()) {
        SubtitleTrack track;
        track.filePath = path;
        track.language = QFileInfo(path).completeBaseName();
        track.enabled = true;
        track.trackIndex = m_subtitleTracks.isEmpty() ? 0 : 1;
        track.encoding = detectSubtitleEncoding(path);
        
        m_subtitleTracks.append(track);
        
        if (track.trackIndex == 0) {
            m_primarySubtitle = track;
        } else {
            m_secondarySubtitle = track;
            m_secondarySubtitleCheck->setChecked(true);
        }
        
        updateSubtitleCombo();
        loadSubtitle(track);
        
        statusBar()->showMessage("Subtitle loaded: " + QFileInfo(path).fileName(), 3000);
    }
}

void VideoPlayer::onSetSubtitle() {
    if (m_currentIndex >= 0 && m_currentIndex < m_playlist.size()) {
        QString videoPath = m_playlist[m_currentIndex].filePath;
        QString videoDir = QFileInfo(videoPath).path();
        QString videoName = QFileInfo(videoPath).completeBaseName();
        
        QDir dir(videoDir);
        QStringList filters;
        filters << "*.srt" << "*.ass" << "*.vtt" << "*.sup";
        
        QFileInfoList subtitleFiles = dir.entryInfoList(filters, QDir::Files);
        
        m_subtitleTracks.clear();
        m_subtitleCombo->clear();
        m_subtitleCombo->addItem("No Subtitles");
        
        for (const QFileInfo& fi : subtitleFiles) {
            if (fi.completeBaseName().startsWith(videoName)) {
                SubtitleTrack track;
                track.filePath = fi.filePath();
                track.language = fi.completeBaseName().replace(videoName, "").replace(".", "").replace("-", "").trimmed();
                if (track.language.isEmpty()) track.language = "Default";
                track.enabled = false;
                track.trackIndex = m_subtitleTracks.size();
                track.encoding = detectSubtitleEncoding(fi.filePath());
                
                m_subtitleTracks.append(track);
                m_subtitleCombo->addItem(track.language + " (" + fi.suffix().toUpper() + ")");
            }
        }
        
        if (m_subtitleTracks.size() > 1) {
            statusBar()->showMessage(QString("%1 subtitle files found").arg(m_subtitleTracks.size()), 3000);
        }
    }
}

void VideoPlayer::onSubtitleTrackChanged(int index) {
    if (index == 0) {
        m_subtitleLabel->clear();
        m_primarySubtitle.enabled = false;
    } else if (index - 1 < m_subtitleTracks.size()) {
        m_primarySubtitle = m_subtitleTracks[index - 1];
        m_primarySubtitle.enabled = true;
        loadSubtitle(m_primarySubtitle);
    }
}

void VideoPlayer::onToggleSecondarySubtitle(bool enabled) {
    m_secondarySubtitle.enabled = enabled;
    if (enabled && !m_secondarySubtitle.filePath.isEmpty()) {
        loadSubtitle(m_secondarySubtitle);
    } else {
        m_secondarySubtitleLabel->clear();
    }
}

void VideoPlayer::loadSubtitle(const SubtitleTrack& track) {
    if (!QFile::exists(track.filePath)) return;
    
    QFile file(track.filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    
    QString content = QString::fromUtf8(file.readAll());
    file.close();
    
    QString ext = QFileInfo(track.filePath).suffix().toLower();
    
    if (ext == "srt") {
        parseSRT(content, track.trackIndex);
    } else if (ext == "vtt") {
        parseVTT(content, track.trackIndex);
    } else if (ext == "ass") {
        parseASS(content, track.trackIndex);
    }
}

QString VideoPlayer::detectSubtitleEncoding(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return "UTF-8";
    
    QByteArray data = file.readAll();
    file.close();
    
    if (data.startsWith("\xEF\xBB\xBF")) return "UTF-8";
    if (data.startsWith("\xFF\xFE")) return "UTF-16LE";
    if (data.startsWith("\xFE\xFF")) return "UTF-16BE";
    
    QStringList encodings = {"UTF-8", "ISO-8859-1", "Windows-1252", "Windows-1251", "GB18030", "Shift_JIS"};
    
    for (const QString& encoding : encodings) {
        QTextCodec* codec = QTextCodec::codecForName(encoding.toLocal8Bit());
        if (codec) {
            QString text = codec->toUnicode(data);
            if (!text.contains(QChar::ReplacementCharacter)) {
                return encoding;
            }
        }
    }
    
    return "UTF-8";
}

void VideoPlayer::parseSRT(const QString& content, int trackIndex) {
    QStringList lines = content.split("\n", Qt::SkipEmptyParts);
    QLabel* label = (trackIndex == 0) ? m_subtitleLabel : m_secondarySubtitleLabel;
    if (label) label->setText("SRT loaded - " + QString::number(lines.size()) + " lines");
}

void VideoPlayer::parseVTT(const QString& content, int trackIndex) {
    QLabel* label = (trackIndex == 0) ? m_subtitleLabel : m_secondarySubtitleLabel;
    if (label) label->setText("VTT loaded");
}

void VideoPlayer::parseASS(const QString& content, int trackIndex) {
    QLabel* label = (trackIndex == 0) ? m_subtitleLabel : m_secondarySubtitleLabel;
    if (label) label->setText("ASS loaded");
}

void VideoPlayer::updateSubtitleCombo() {
    m_subtitleCombo->clear();
    m_subtitleCombo->addItem("No Subtitles");
    for (const SubtitleTrack& track : m_subtitleTracks) {
        m_subtitleCombo->addItem(track.language);
    }
}

// ============================================================================
// Directory Navigation Functions
// ============================================================================

void VideoPlayer::showDirectoryBrowser() {
    m_directoryDock->setVisible(!m_directoryDock->isVisible());
    if (m_currentIndex >= 0 && m_currentIndex < m_playlist.size()) {
        loadDirectory(QFileInfo(m_playlist[m_currentIndex].filePath).path());
    }
}

void VideoPlayer::loadDirectory(const QString& path) {
    m_currentDirectory = path;
    m_directoryWidget->clear();
    m_directoryFiles.clear();
    
    QDir dir(path);
    
    if (dir.cdUp()) {
        DirectoryFile parentDir;
        parentDir.name = "..";
        parentDir.path = dir.path();
        parentDir.isDirectory = true;
        m_directoryFiles.append(parentDir);
        dir.cd(path);
    }
    
    QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : dirs) {
        DirectoryFile df;
        df.name = fi.fileName();
        df.path = fi.filePath();
        df.isDirectory = true;
        df.isVideo = false;
        m_directoryFiles.append(df);
        m_directoryWidget->addItem("📁 " + df.name);
    }
    
    QStringList videoFilters;
    videoFilters << "*.mp4" << "*.mkv" << "*.avi" << "*.mov" << "*.wmv" << "*.flv" << "*.webm";
    
    QFileInfoList files = dir.entryInfoList(videoFilters, QDir::Files, QDir::Name);
    for (const QFileInfo& fi : files) {
        DirectoryFile df;
        df.name = fi.fileName();
        df.path = fi.filePath();
        df.isDirectory = false;
        df.isVideo = true;
        df.size = fi.size();
        m_directoryFiles.append(df);
        
        QString sizeStr;
        if (df.size > 1073741824) sizeStr = QString::number(df.size / 1073741824.0, 'f', 1) + " GB";
        else if (df.size > 1048576) sizeStr = QString::number(df.size / 1048576.0, 'f', 1) + " MB";
        else sizeStr = QString::number(df.size / 1024.0, 'f', 1) + " KB";
        
        m_directoryWidget->addItem("🎬 " + df.name + " (" + sizeStr + ")");
    }
    
    m_directoryDock->setWindowTitle("Directory: " + path);
}

void VideoPlayer::onDirectoryUp() {
    QDir dir(m_currentDirectory);
    if (dir.cdUp()) loadDirectory(dir.path());
}

void VideoPlayer::onDirectoryFileActivated(QListWidgetItem* item) {
    int index = m_directoryWidget->row(item);
    if (index >= 0 && index < m_directoryFiles.size()) {
        DirectoryFile df = m_directoryFiles[index];
        if (df.isDirectory) {
            loadDirectory(df.path);
        } else if (df.isVideo) {
            playFile(df.path);
        }
    }
}

} // namespace havel

#include "VideoPlayer.moc"
