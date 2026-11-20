// Screenshot Application for Havel WM

#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QProgressBar>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QPixmap>
#include <QRect>
#include <QPainter>
#include <QPainterPath>
#include <QRubberBand>
#include <QScreen>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QDateTime>
#include <QScrollArea>
#include <QToolBar>
#include <QStatusBar>
#include <QKeyEvent>
#include <QMouseEvent>

namespace havel {

/**
 * Screenshot capture modes
 */
enum class CaptureMode {
    FullScreen,
    ActiveWindow,
    SelectedRegion,
    SingleWindow,
    PerMonitor,
    AllMonitors
};

/**
 * Screenshot format
 */
enum class ImageFormat {
    PNG,
    JPEG,
    BMP,
    WEBP
};

/**
 * Region selector overlay
 */
class RegionSelector : public QWidget {
    Q_OBJECT

public:
    explicit RegionSelector(QWidget* parent = nullptr);
    
    void startSelection();
    QRect selectedRegion() const { return m_selection; }
    bool hasSelection() const { return !m_selection.isNull(); }
    
signals:
    void regionSelected(const QRect& region);
    void selectionCancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateOverlay();
    
    QPixmap m_screen;
    QRect m_selection;
    QPoint m_startPoint;
    bool m_selecting;
    QRubberBand* m_rubberBand;
};

/**
 * Screenshot editor/annotator
 */
class ScreenshotEditor : public QMainWindow {
    Q_OBJECT

public:
    explicit ScreenshotEditor(const QPixmap& screenshot, QWidget* parent = nullptr);
    
    QPixmap editedScreenshot() const { return m_screenshot; }
    
private slots:
    void onDrawFreehand();
    void onDrawArrow();
    void onDrawRectangle();
    void onDrawEllipse();
    void onAddText();
    void onDrawNumber();
    void onUseLens();
    void onChangeColor();
    void onChangeSize(int size);
    void onUndo();
    void onRedo();
    void onClearAnnotations();
    void onSave();
    void onSaveAs();
    void onCopy();
    void onClose();

private:
    void setupUI();
    void setupTools();
    void setupEditor();
    void drawOnImage();
    
    QLabel* m_imageLabel;
    QPixmap m_screenshot;
    QPixmap m_displayPixmap;
    
    // Annotation state
    QVector<QPixmap> m_undoStack;
    QVector<QPixmap> m_redoStack;
    
    // Drawing tools
    enum class Tool { None, Freehand, Arrow, Rectangle, Ellipse, Text, Number, Lens };
    Tool m_currentTool;
    QColor m_drawColor;
    int m_drawSize;
    
    // Drawing state
    bool m_drawing;
    QPoint m_lastPoint;
    
    // Toolbar
    QToolBar* m_toolBar;
    QAction* m_freehandAction;
    QAction* m_arrowAction;
    QAction* m_rectAction;
    QAction* m_ellipseAction;
    QAction* m_textAction;
    QAction* m_numberAction;
    QAction* m_lensAction;
    QComboBox* m_colorCombo;
    QSpinBox* m_sizeSpin;
};

/**
 * Main Screenshot Window
 */
class ScreenshotWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ScreenshotWindow(QWidget* parent = nullptr);
    ~ScreenshotWindow();
    
public slots:
    // Capture
    void onCaptureFullScreen();
    void onCaptureWindow();
    void onCaptureRegion();
    void onCapturePerMonitor();
    void onCaptureAllMonitors();
    void onCaptureDelayed();
    
    // Timer completion
    void onCaptureTimer();
    
    // Output
    void onSave();
    void onSaveAs();
    void onCopy();
    void onOpenEditor();
    
    // Settings
    void onChangeFormat(int index);
    void onChangeQuality(int quality);
    void onChangeDelay(int delay);
    void onToggleCursor(bool show);
    void onToggleNotifications(bool enable);
    
    // System tray
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onShowWindow();
    void onHideWindow();
    void onQuit();
    
    // Help
    void onAbout();
    void onShortcuts();

private:
    void setupUI();
    void setupActions();
    void setupMenuBar();
    void setupSystemTray();
    void loadSettings();
    void saveSettings();
    
    QPixmap captureFullScreen();
    QPixmap captureWindow();
    QPixmap captureRegion();
    void saveScreenshot(const QPixmap& screenshot);
    void showNotification(const QString& message);
    
    // UI components
    QLabel* m_previewLabel;
    QPushButton* m_captureButton;
    QPushButton* m_fullScreenButton;
    QPushButton* m_windowButton;
    QPushButton* m_regionButton;
    QPushButton* m_perMonitorButton;
    QPushButton* m_allMonitorsButton;
    QPushButton* m_delayButton;
    
    QSpinBox* m_delaySpin;
    QComboBox* m_formatCombo;
    QSpinBox* m_qualitySpin;
    QCheckBox* m_includeCursorCheck;
    QCheckBox* m_notificationsCheck;
    QCheckBox* m_minimizeCheck;
    
    QLineEdit* m_savePathEdit;
    QPushButton* m_browseButton;
    
    QProgressBar* m_countdownProgress;
    QLabel* m_countdownLabel;
    
    QMenuBar* m_menuBar;
    QMenu* m_fileMenu;
    QMenu* m_editMenu;
    QMenu* m_viewMenu;
    QMenu* m_helpMenu;
    
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_trayMenu;
    
    // Capture state
    CaptureMode m_captureMode;
    ImageFormat m_imageFormat;
    int m_captureDelay;
    bool m_includeCursor;
    bool m_showNotifications;
    bool m_minimizeOnCapture;
    
    QTimer* m_captureTimer;
    int m_remainingDelay;
    
    QPixmap m_lastScreenshot;
    QString m_saveDirectory;
};

} // namespace havel
