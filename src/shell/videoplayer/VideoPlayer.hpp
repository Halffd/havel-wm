// Video Player for Havel WM

#pragma once

#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QToolBar>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QSystemTrayIcon>
#include <QSettings>
#include <QProcess>
#include <QTimer>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QProgressDialog>
#include <QScrollArea>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QDockWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QStackedWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QIcon>
#include <QPainter>
#include <QStyleFactory>
#include <QPalette>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QLabel>
#include <QAudioOutput>
#include <QMediaMetaData>

namespace havel {

/**
 * Video file information
 */
struct VideoInfo {
    QString title;
    QString filePath;
    QString duration;
    QString resolution;
    QString codec;
    QString size;
    QDateTime addedDate;
    QDateTime lastPlayed;
    int playCount;
    int position;  // Last position in ms
    QString thumbnail;
    QStringList subtitles;
    
    VideoInfo() : playCount(0), position(0) {}
    VideoInfo(std::nullptr_t) : playCount(0), position(0) {}
};

/**
 * Subtitle track
 */
struct SubtitleTrack {
    QString language;
    QString filePath;
    bool enabled;
    
    SubtitleTrack() : enabled(false) {}
};

/**
 * Playback settings
 */
struct PlaybackSettings {
    float speed;
    int volume;
    bool muted;
    bool loop;
    bool shuffle;
    bool rememberPosition;
    bool autoPlay;
    QString audioTrack;
    QString subtitleTrack;
    bool fullscreenOnPlay;
    bool minimizeOnClose;
    
    PlaybackSettings() : speed(1.0f), volume(100), muted(false), 
                         loop(false), shuffle(false), rememberPosition(true),
                         autoPlay(true), fullscreenOnPlay(false), minimizeOnClose(true) {}
};

/**
 * Main Video Player Window
 */
class VideoPlayer : public QMainWindow {
    Q_OBJECT

public:
    explicit VideoPlayer(QWidget* parent = nullptr);
    ~VideoPlayer();
    
    // CLI support
    void playFile(const QString& filePath);
    void addToPlaylist(const QString& filePath);
    void showPlaylist();
    void setVolume(int volume);
    void setFullscreen(bool fullscreen);
    
private slots:
    // File operations
    void onOpenFile();
    void onOpenFolder();
    void onOpenURL();
    void onOpenRecent();
    void onClose();
    
    // Playback control
    void onPlay();
    void onPause();
    void onStop();
    void onPrevious();
    void onNext();
    void onSeek(int position);
    void onVolumeChanged(int volume);
    void onMute();
    void onSpeedChanged();
    
    // Video control
    void onFullscreen();
    void onAspectRatio();
    void onZoom();
    void onScreenshot();
    void onSetSubtitle();
    
    // Playlist
    void onShowPlaylist();
    void onClearPlaylist();
    void onShuffle();
    void onLoop();
    void onPlaylistItemActivated(QListWidgetItem* item);
    void onPlaylistItemRemoved(int row);
    
    // Media player signals
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void onErrorOccurred(QMediaPlayer::Error error);
    void onVideoOutputReady();
    
    // System tray
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    
    // Settings
    void onPreferences();
    void onResetSettings();
    
    // Help
    void onAbout();
    void onShortcuts();

private:
    void setupUI();
    void setupMediaPlayer();
    void setupMenu();
    void setupToolbar();
    void setupStatusBar();
    void setupSystemTray();
    void setupConnections();
    void setupGestureControls();
    
    void loadSettings();
    void saveSettings();
    void loadPlaylist();
    void savePlaylist();
    void loadRecentFiles();
    void saveRecentFiles();
    
    void updateControls();
    void updateProgress();
    void updateTimeLabels();
    void formatTime(qint64 ms, QLabel* label);
    void showVideoInfo();
    
    void addToRecent(const QString& filePath);
    void loadVideoInfo(const QString& filePath);
    QString getThumbnail(const QString& videoPath);
    
    // UI components
    QStackedWidget* m_centralStack;
    
    // Video widget
    QWidget* m_videoContainer;
    QLabel* m_videoWidget;
    QWidget* m_controlsWidget;
    
    // Controls
    QSlider* m_progressSlider;
    QSlider* m_volumeSlider;
    QPushButton* m_playButton;
    QPushButton* m_pauseButton;
    QPushButton* m_stopButton;
    QPushButton* m_previousButton;
    QPushButton* m_nextButton;
    QPushButton* m_fullscreenButton;
    QPushButton* m_volumeButton;
    QComboBox* m_speedCombo;
    QLabel* m_timeLabel;
    QLabel* m_durationLabel;
    QLabel* m_titleLabel;
    
    // Playlist dock
    QDockWidget* m_playlistDock;
    QListWidget* m_playlistWidget;
    QPushButton* m_clearPlaylistButton;
    
    // Toolbar
    QToolBar* m_mainToolBar;
    QAction* m_openAction;
    QAction* m_playAction;
    QAction* m_pauseAction;
    QAction* m_stopAction;
    QAction* m_fullscreenAction;
    QAction* m_playlistAction;
    
    // Status bar
    QLabel* m_statusLabel;
    QLabel* m_resolutionLabel;
    QLabel* m_positionLabel;
    
    // System tray
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_trayMenu;
    
    // Media player
    QMediaPlayer* m_mediaPlayer;
    QList<QUrl> m_playlistUrls;
    QAudioOutput* m_audioOutput;
    
    // Network
    QNetworkAccessManager* m_networkManager;
    
    // Data
    QList<VideoInfo> m_playlist;
    QStringList m_recentFiles;
    PlaybackSettings m_settings;
    int m_currentIndex;
    bool m_seeking;
    
    // State
    bool m_fullscreen;
    bool m_controlsVisible;
    QTimer* m_controlsTimer;
};

/**
 * Preferences dialog
 */
class VideoPreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit VideoPreferencesDialog(const PlaybackSettings& settings, QWidget* parent = nullptr);
    PlaybackSettings getSettings() const { return m_settings; }
    
private:
    void setupUI();
    
    PlaybackSettings m_settings;
    
    QCheckBox* m_rememberPosition;
    QCheckBox* m_autoPlay;
    QCheckBox* m_fullscreenOnPlay;
    QCheckBox* m_minimizeOnClose;
    QSpinBox* m_volumeSpin;
    QDoubleSpinBox* m_speedSpin;
};

} // namespace havel
