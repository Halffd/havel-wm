// Screenshot Application Implementation

#include "Screenshot.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QScreen>
#include <QWindow>
#include <QCursor>
#include <QPainter>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QMenuBar>
#include <QInputDialog>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace havel {

// ============================================================================
// RegionSelector Implementation
// ============================================================================

RegionSelector::RegionSelector(QWidget* parent)
    : QWidget(parent)
    , m_selecting(false)
    , m_rubberBand(nullptr)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::CrossCursor);
    
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
}

void RegionSelector::startSelection() {
    // Capture screen
    QScreen* screen = QApplication::primaryScreen();
    m_screen = screen->grabWindow(0);
    
    // Show full screen
    setGeometry(screen->geometry());
    show();
    raise();
    activateWindow();
    
    m_selecting = false;
    m_selection = QRect();
}

void RegionSelector::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    
    // Draw screen capture
    painter.drawPixmap(0, 0, m_screen);
    
    // Draw dark overlay
    painter.setBrush(QColor(0, 0, 0, 100));
    painter.setPen(Qt::NoPen);
    
    if (m_selecting && !m_selection.isNull()) {
        // Clear selection area
        QPainterPath path;
        path.addRect(rect());
        path.addRect(m_selection);
        painter.drawPath(path);
        
        // Draw selection border
        painter.setPen(QPen(Qt::green, 2));
        painter.drawRect(m_selection);
        
        // Draw size info
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPointSize(12);
        font.setBold(true);
        painter.setFont(font);
        QString info = QString("%1 x %2").arg(m_selection.width()).arg(m_selection.height());
        painter.drawText(m_selection.topRight() + QPoint(5, 20), info);
    } else {
        painter.drawRect(rect());
    }
}

void RegionSelector::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_startPoint = event->pos();
        m_selecting = true;
        m_selection = QRect();
        m_rubberBand->setGeometry(QRect(m_startPoint, QSize()));
        m_rubberBand->show();
    }
}

void RegionSelector::mouseMoveEvent(QMouseEvent* event) {
    if (m_selecting) {
        m_selection = QRect(m_startPoint, event->pos()).normalized();
        m_rubberBand->setGeometry(m_selection);
        update();
    }
}

void RegionSelector::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_selecting) {
        m_selecting = false;
        m_rubberBand->hide();
        
        if (!m_selection.isNull()) {
            emit regionSelected(m_selection);
        }
        close();
    }
}

void RegionSelector::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        m_selecting = false;
        m_rubberBand->hide();
        emit selectionCancelled();
        close();
    }
}

// ============================================================================
// ScreenshotEditor Implementation
// ============================================================================

ScreenshotEditor::ScreenshotEditor(const QPixmap& screenshot, QWidget* parent)
    : QMainWindow(parent)
    , m_screenshot(screenshot)
    , m_displayPixmap(screenshot)
    , m_currentTool(Tool::None)
    , m_drawColor(Qt::red)
    , m_drawSize(3)
    , m_drawing(false)
{
    setWindowTitle("Screenshot Editor - Havel WM");
    setMinimumSize(800, 600);
    
    setupUI();
    setupTools();
    setupEditor();
}

void ScreenshotEditor::setupUI() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    
    QVBoxLayout* layout = new QVBoxLayout(central);
    
    // Image display
    m_imageLabel = new QLabel();
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setPixmap(m_displayPixmap);
    
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidget(m_imageLabel);
    scrollArea->setWidgetResizable(true);
    
    layout->addWidget(scrollArea);
}

void ScreenshotEditor::setupTools() {
    m_toolBar = addToolBar("Tools");
    
    m_freehandAction = m_toolBar->addAction("Freehand");
    m_freehandAction->setCheckable(true);
    connect(m_freehandAction, &QAction::triggered, this, &ScreenshotEditor::onDrawFreehand);
    
    m_arrowAction = m_toolBar->addAction("Arrow");
    m_arrowAction->setCheckable(true);
    connect(m_arrowAction, &QAction::triggered, this, &ScreenshotEditor::onDrawArrow);
    
    m_rectAction = m_toolBar->addAction("Rectangle");
    m_rectAction->setCheckable(true);
    connect(m_rectAction, &QAction::triggered, this, &ScreenshotEditor::onDrawRectangle);
    
    m_ellipseAction = m_toolBar->addAction("Ellipse");
    m_ellipseAction->setCheckable(true);
    connect(m_ellipseAction, &QAction::triggered, this, &ScreenshotEditor::onDrawEllipse);
    
    m_textAction = m_toolBar->addAction("Text");
    m_textAction->setCheckable(true);
    connect(m_textAction, &QAction::triggered, this, &ScreenshotEditor::onAddText);
    
    m_numberAction = m_toolBar->addAction("Number");
    m_numberAction->setCheckable(true);
    connect(m_numberAction, &QAction::triggered, this, &ScreenshotEditor::onDrawNumber);
    
    m_toolBar->addSeparator();
    
    // Color selector
    m_toolBar->addWidget(new QLabel(" Color: "));
    m_colorCombo = new QComboBox();
    m_colorCombo->addItem("Red", QVariant::fromValue(QColor(Qt::red)));
    m_colorCombo->addItem("Green", QVariant::fromValue(QColor(Qt::green)));
    m_colorCombo->addItem("Blue", QVariant::fromValue(QColor(Qt::blue)));
    m_colorCombo->addItem("Yellow", QVariant::fromValue(QColor(Qt::yellow)));
    m_colorCombo->addItem("White", QVariant::fromValue(QColor(Qt::white)));
    m_colorCombo->addItem("Black", QVariant::fromValue(QColor(Qt::black)));
    m_colorCombo->setCurrentIndex(0);
    connect(m_colorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ScreenshotEditor::onChangeColor);
    m_toolBar->addWidget(m_colorCombo);
    
    // Size selector
    m_toolBar->addWidget(new QLabel("  Size: "));
    m_sizeSpin = new QSpinBox();
    m_sizeSpin->setRange(1, 20);
    m_sizeSpin->setValue(3);
    connect(m_sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ScreenshotEditor::onChangeSize);
    m_toolBar->addWidget(m_sizeSpin);
    
    m_toolBar->addSeparator();
    
    // Undo/Redo
    m_toolBar->addAction("Undo", this, &ScreenshotEditor::onUndo);
    m_toolBar->addAction("Redo", this, &ScreenshotEditor::onRedo);
    m_toolBar->addAction("Clear", this, &ScreenshotEditor::onClearAnnotations);
    
    m_toolBar->addSeparator();
    
    // Save/Copy
    m_toolBar->addAction("Save", this, &ScreenshotEditor::onSave);
    m_toolBar->addAction("Save As", this, &ScreenshotEditor::onSaveAs);
    m_toolBar->addAction("Copy", this, &ScreenshotEditor::onCopy);
    m_toolBar->addAction("Close", this, &ScreenshotEditor::onClose);
}

void ScreenshotEditor::setupEditor() {
    // Save initial state for undo
    m_undoStack.append(m_screenshot);
}

void ScreenshotEditor::onDrawFreehand() {
    m_currentTool = Tool::Freehand;
    m_freehandAction->setChecked(true);
}

void ScreenshotEditor::onDrawArrow() {
    m_currentTool = Tool::Arrow;
    m_arrowAction->setChecked(true);
}

void ScreenshotEditor::onDrawRectangle() {
    m_currentTool = Tool::Rectangle;
    m_rectAction->setChecked(true);
}

void ScreenshotEditor::onDrawEllipse() {
    m_currentTool = Tool::Ellipse;
    m_ellipseAction->setChecked(true);
}

void ScreenshotEditor::onAddText() {
    m_currentTool = Tool::Text;
    m_textAction->setChecked(true);
}

void ScreenshotEditor::onDrawNumber() {
    m_currentTool = Tool::Number;
    m_numberAction->setChecked(true);
}

void ScreenshotEditor::onChangeColor() {
    m_drawColor = m_colorCombo->currentData().value<QColor>();
}

void ScreenshotEditor::onChangeSize(int size) {
    m_drawSize = size;
}

void ScreenshotEditor::onUndo() {
    if (m_undoStack.size() > 1) {
        m_redoStack.append(m_screenshot);
        m_screenshot = m_undoStack.takeLast();
        m_displayPixmap = m_screenshot;
        m_imageLabel->setPixmap(m_displayPixmap);
    }
}

void ScreenshotEditor::onRedo() {
    if (!m_redoStack.isEmpty()) {
        m_undoStack.append(m_screenshot);
        m_screenshot = m_redoStack.takeLast();
        m_displayPixmap = m_screenshot;
        m_imageLabel->setPixmap(m_displayPixmap);
    }
}

void ScreenshotEditor::onClearAnnotations() {
    if (!m_undoStack.isEmpty()) {
        m_redoStack.append(m_screenshot);
        m_screenshot = m_undoStack.first();
        m_displayPixmap = m_screenshot;
        m_imageLabel->setPixmap(m_displayPixmap);
    }
}

void ScreenshotEditor::onSave() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    path += "/screenshot_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";
    m_screenshot.save(path);
}

void ScreenshotEditor::onSaveAs() {
    QString path = QFileDialog::getSaveFileName(this, "Save Screenshot", "",
        "PNG Images (*.png);;JPEG Images (*.jpg);;BMP Images (*.bmp);;All Files (*)");
    if (!path.isEmpty()) {
        m_screenshot.save(path);
    }
}

void ScreenshotEditor::onCopy() {
    QApplication::clipboard()->setPixmap(m_screenshot);
}

void ScreenshotEditor::onClose() {
    close();
}

// ============================================================================
// ScreenshotWindow Implementation
// ============================================================================

ScreenshotWindow::ScreenshotWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_previewLabel(nullptr)
    , m_captureButton(nullptr)
    , m_fullScreenButton(nullptr)
    , m_windowButton(nullptr)
    , m_regionButton(nullptr)
    , m_delayButton(nullptr)
    , m_delaySpin(nullptr)
    , m_formatCombo(nullptr)
    , m_qualitySpin(nullptr)
    , m_includeCursorCheck(nullptr)
    , m_notificationsCheck(nullptr)
    , m_minimizeCheck(nullptr)
    , m_savePathEdit(nullptr)
    , m_browseButton(nullptr)
    , m_countdownProgress(nullptr)
    , m_countdownLabel(nullptr)
    , m_captureMode(CaptureMode::FullScreen)
    , m_imageFormat(ImageFormat::PNG)
    , m_captureDelay(0)
    , m_includeCursor(true)
    , m_showNotifications(true)
    , m_minimizeOnCapture(false)
    , m_captureTimer(nullptr)
    , m_remainingDelay(0)
{
    setWindowTitle("Screenshot - Havel WM");
    setMinimumSize(500, 600);
    
    setupUI();
    setupActions();
    setupMenuBar();
    setupSystemTray();
    
    loadSettings();
}

ScreenshotWindow::~ScreenshotWindow() {
    saveSettings();
}

void ScreenshotWindow::setupUI() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    
    QVBoxLayout* layout = new QVBoxLayout(central);
    
    // Preview
    QGroupBox* previewGroup = new QGroupBox("Preview");
    QVBoxLayout* previewLayout = new QVBoxLayout(previewGroup);
    
    m_previewLabel = new QLabel();
    m_previewLabel->setMinimumHeight(200);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setText("No screenshot captured");
    m_previewLabel->setStyleSheet("QLabel { background-color: #202020; color: #808080; }");
    
    previewLayout->addWidget(m_previewLabel);
    layout->addWidget(previewGroup);
    
    // Capture options
    QGroupBox* captureGroup = new QGroupBox("Capture");
    QHBoxLayout* captureLayout = new QHBoxLayout(captureGroup);
    
    m_fullScreenButton = new QPushButton("Full Screen");
    m_fullScreenButton->setIcon(QIcon::fromTheme("video-display"));
    connect(m_fullScreenButton, &QPushButton::clicked, this, &ScreenshotWindow::onCaptureFullScreen);
    captureLayout->addWidget(m_fullScreenButton);
    
    m_windowButton = new QPushButton("Active Window");
    m_windowButton->setIcon(QIcon::fromTheme("window"));
    connect(m_windowButton, &QPushButton::clicked, this, &ScreenshotWindow::onCaptureWindow);
    captureLayout->addWidget(m_windowButton);
    
    m_regionButton = new QPushButton("Select Region");
    m_regionButton->setIcon(QIcon::fromTheme("select-rectangular"));
    connect(m_regionButton, &QPushButton::clicked, this, &ScreenshotWindow::onCaptureRegion);
    captureLayout->addWidget(m_regionButton);
    
    layout->addWidget(captureGroup);
    
    // Delay
    QGroupBox* delayGroup = new QGroupBox("Delay");
    QHBoxLayout* delayLayout = new QHBoxLayout(delayGroup);
    
    delayLayout->addWidget(new QLabel("Delay:"));
    m_delaySpin = new QSpinBox();
    m_delaySpin->setRange(0, 60);
    m_delaySpin->setSuffix(" seconds");
    m_delaySpin->setValue(0);
    connect(m_delaySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ScreenshotWindow::onChangeDelay);
    delayLayout->addWidget(m_delaySpin);
    
    m_delayButton = new QPushButton("Capture Delayed");
    connect(m_delayButton, &QPushButton::clicked, this, &ScreenshotWindow::onCaptureDelayed);
    delayLayout->addWidget(m_delayButton);
    
    delayLayout->addStretch();
    
    // Countdown (hidden by default)
    m_countdownProgress = new QProgressBar();
    m_countdownProgress->setVisible(false);
    m_countdownProgress->setRange(0, 60);
    delayLayout->addWidget(m_countdownProgress);
    
    m_countdownLabel = new QLabel("");
    m_countdownLabel->setVisible(false);
    delayLayout->addWidget(m_countdownLabel);
    
    layout->addWidget(delayGroup);
    
    // Options
    QGroupBox* optionsGroup = new QGroupBox("Options");
    QFormLayout* optionsLayout = new QFormLayout(optionsGroup);
    
    // Format
    m_formatCombo = new QComboBox();
    m_formatCombo->addItem("PNG", static_cast<int>(ImageFormat::PNG));
    m_formatCombo->addItem("JPEG", static_cast<int>(ImageFormat::JPEG));
    m_formatCombo->addItem("BMP", static_cast<int>(ImageFormat::BMP));
    m_formatCombo->addItem("WEBP", static_cast<int>(ImageFormat::WEBP));
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ScreenshotWindow::onChangeFormat);
    optionsLayout->addRow("Format:", m_formatCombo);
    
    // Quality (for JPEG/WEBP)
    m_qualitySpin = new QSpinBox();
    m_qualitySpin->setRange(1, 100);
    m_qualitySpin->setValue(90);
    m_qualitySpin->setSuffix("%");
    connect(m_qualitySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ScreenshotWindow::onChangeQuality);
    optionsLayout->addRow("Quality:", m_qualitySpin);
    
    // Include cursor
    m_includeCursorCheck = new QCheckBox("Include cursor");
    m_includeCursorCheck->setChecked(true);
    connect(m_includeCursorCheck, &QCheckBox::toggled, this, &ScreenshotWindow::onToggleCursor);
    optionsLayout->addRow(m_includeCursorCheck);
    
    // Notifications
    m_notificationsCheck = new QCheckBox("Show notifications");
    m_notificationsCheck->setChecked(true);
    connect(m_notificationsCheck, &QCheckBox::toggled, this, &ScreenshotWindow::onToggleNotifications);
    optionsLayout->addRow(m_notificationsCheck);
    
    // Minimize on capture
    m_minimizeCheck = new QCheckBox("Minimize window on capture");
    connect(m_minimizeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_minimizeOnCapture = checked;
    });
    optionsLayout->addRow(m_minimizeCheck);
    
    layout->addWidget(optionsGroup);
    
    // Save options
    QGroupBox* saveGroup = new QGroupBox("Save Options");
    QHBoxLayout* saveLayout = new QHBoxLayout(saveGroup);
    
    saveLayout->addWidget(new QLabel("Save to:"));
    m_savePathEdit = new QLineEdit();
    m_savePathEdit->setText(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    saveLayout->addWidget(m_savePathEdit);
    
    m_browseButton = new QPushButton("Browse...");
    connect(m_browseButton, &QPushButton::clicked, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Save Directory",
            m_saveDirectory);
        if (!dir.isEmpty()) {
            m_savePathEdit->setText(dir);
            m_saveDirectory = dir;
        }
    });
    saveLayout->addWidget(m_browseButton);
    
    layout->addWidget(saveGroup);
    
    // Action buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    QPushButton* saveButton = new QPushButton("Save");
    connect(saveButton, &QPushButton::clicked, this, &ScreenshotWindow::onSave);
    buttonLayout->addWidget(saveButton);
    
    QPushButton* copyButton = new QPushButton("Copy to Clipboard");
    connect(copyButton, &QPushButton::clicked, this, &ScreenshotWindow::onCopy);
    buttonLayout->addWidget(copyButton);
    
    QPushButton* editButton = new QPushButton("Edit...");
    connect(editButton, &QPushButton::clicked, this, &ScreenshotWindow::onOpenEditor);
    buttonLayout->addWidget(editButton);
    
    layout->addLayout(buttonLayout);
    layout->addStretch();
}

void ScreenshotWindow::setupActions() {
    // Create timer for delayed capture
    m_captureTimer = new QTimer(this);
    m_captureTimer->setSingleShot(true);
    connect(m_captureTimer, &QTimer::timeout, this, &ScreenshotWindow::onCaptureTimer);
}

void ScreenshotWindow::setupMenuBar() {
    m_menuBar = menuBar();
    
    m_fileMenu = m_menuBar->addMenu("&File");
    m_fileMenu->addAction("&Save", this, &ScreenshotWindow::onSave, QKeySequence::Save);
    m_fileMenu->addAction("Save &As...", this, &ScreenshotWindow::onSaveAs, QKeySequence::SaveAs);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction("E&xit", this, &QMainWindow::close, QKeySequence::Quit);
    
    m_editMenu = m_menuBar->addMenu("&Edit");
    m_editMenu->addAction("&Copy", this, &ScreenshotWindow::onCopy, QKeySequence::Copy);
    m_editMenu->addAction("&Edit...", this, &ScreenshotWindow::onOpenEditor);
    
    m_viewMenu = m_menuBar->addMenu("&View");
    m_viewMenu->addAction("Show in &System Tray", [this]() {
        m_trayIcon->show();
    });
    
    m_helpMenu = m_menuBar->addMenu("&Help");
    m_helpMenu->addAction("&About", this, &ScreenshotWindow::onAbout);
    m_helpMenu->addAction("&Keyboard Shortcuts", this, &ScreenshotWindow::onShortcuts);
}

void ScreenshotWindow::setupSystemTray() {
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(QIcon::fromTheme("camera-photo", QIcon::fromTheme("accessories-screenshot")));
    
    m_trayMenu = new QMenu(this);
    m_trayMenu->addAction("Show", this, &ScreenshotWindow::onShowWindow);
    m_trayMenu->addAction("Hide", this, &ScreenshotWindow::onHideWindow);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction("Capture Full Screen", this, &ScreenshotWindow::onCaptureFullScreen);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction("Quit", this, &ScreenshotWindow::onQuit);
    
    m_trayIcon->setContextMenu(m_trayMenu);
    
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &ScreenshotWindow::onTrayActivated);
}

void ScreenshotWindow::loadSettings() {
    QSettings settings("Havel WM", "Screenshot");
    
    m_saveDirectory = settings.value("saveDirectory", 
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString();
    m_savePathEdit->setText(m_saveDirectory);
    
    int formatIndex = settings.value("format", 0).toInt();
    m_formatCombo->setCurrentIndex(formatIndex);
    
    int quality = settings.value("quality", 90).toInt();
    m_qualitySpin->setValue(quality);
    
    m_includeCursor = settings.value("includeCursor", true).toBool();
    m_includeCursorCheck->setChecked(m_includeCursor);
    
    m_showNotifications = settings.value("notifications", true).toBool();
    m_notificationsCheck->setChecked(m_showNotifications);
}

void ScreenshotWindow::saveSettings() {
    QSettings settings("Havel WM", "Screenshot");
    
    settings.setValue("saveDirectory", m_saveDirectory);
    settings.setValue("format", m_formatCombo->currentIndex());
    settings.setValue("quality", m_qualitySpin->value());
    settings.setValue("includeCursor", m_includeCursor);
    settings.setValue("notifications", m_showNotifications);
}

void ScreenshotWindow::onCaptureFullScreen() {
    m_captureMode = CaptureMode::FullScreen;
    
    if (m_minimizeOnCapture) {
        showMinimized();
        QTimer::singleShot(300, this, [this]() {
            QPixmap screenshot = captureFullScreen();
            showNormal();
            activateWindow();
            saveScreenshot(screenshot);
        });
    } else {
        QPixmap screenshot = captureFullScreen();
        saveScreenshot(screenshot);
    }
}

void ScreenshotWindow::onCaptureWindow() {
    m_captureMode = CaptureMode::ActiveWindow;
    QPixmap screenshot = captureWindow();
    saveScreenshot(screenshot);
}

void ScreenshotWindow::onCaptureRegion() {
    m_captureMode = CaptureMode::SelectedRegion;
    
    RegionSelector* selector = new RegionSelector(this);
    connect(selector, &RegionSelector::regionSelected, [this](const QRect& region) {
        QScreen* screen = QApplication::primaryScreen();
        QPixmap screenshot = screen->grabWindow(0, region.x(), region.y(), region.width(), region.height());
        saveScreenshot(screenshot);
    });
    
    selector->startSelection();
}

void ScreenshotWindow::onCaptureDelayed() {
    m_remainingDelay = m_captureDelay;
    
    if (m_remainingDelay <= 0) {
        m_remainingDelay = 5;  // Default 5 seconds
    }
    
    // Show countdown
    m_countdownProgress->setVisible(true);
    m_countdownProgress->setValue(m_remainingDelay);
    m_countdownLabel->setVisible(true);
    m_countdownLabel->setText(QString("%1 seconds...").arg(m_remainingDelay));
    
    m_captureTimer->start(1000);
}

void ScreenshotWindow::onCaptureTimer() {
    m_remainingDelay--;
    m_countdownProgress->setValue(m_remainingDelay);
    m_countdownLabel->setText(QString("%1 seconds...").arg(m_remainingDelay));
    
    if (m_remainingDelay > 0) {
        m_captureTimer->start(1000);
    } else {
        // Hide countdown
        m_countdownProgress->setVisible(false);
        m_countdownLabel->setVisible(false);
        
        // Capture based on last mode
        switch (m_captureMode) {
            case CaptureMode::FullScreen:
                onCaptureFullScreen();
                break;
            case CaptureMode::ActiveWindow:
                onCaptureWindow();
                break;
            case CaptureMode::SelectedRegion:
                onCaptureRegion();
                break;
            default:
                onCaptureFullScreen();
        }
    }
}

QPixmap ScreenshotWindow::captureFullScreen() {
    QScreen* screen = QApplication::primaryScreen();
    QPixmap screenshot = screen->grabWindow(0);
    
    if (m_includeCursor) {
        QPainter painter(&screenshot);
        QCursor cursor;
        painter.drawPixmap(QCursor::pos() - screenshot.rect().topLeft(), cursor.pixmap());
    }
    
    return screenshot;
}

QPixmap ScreenshotWindow::captureWindow() {
    QWindow* window = QApplication::focusWindow();
    if (!window) {
        window = QApplication::topLevelWidgets().first()->windowHandle();
    }
    
    if (window) {
        QScreen* screen = window->screen();
        return screen->grabWindow(window->winId());
    }
    
    return captureFullScreen();
}

QPixmap ScreenshotWindow::captureRegion() {
    // Handled by RegionSelector
    return QPixmap();
}

void ScreenshotWindow::saveScreenshot(const QPixmap& screenshot) {
    if (screenshot.isNull()) return;
    
    m_lastScreenshot = screenshot;
    
    // Update preview
    m_previewLabel->setPixmap(screenshot.scaled(m_previewLabel->size(), 
        Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_previewLabel->setText("");
    m_previewLabel->setStyleSheet("");
    
    // Generate filename
    QString filename = "screenshot_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    
    // Get format
    ImageFormat format = static_cast<ImageFormat>(m_formatCombo->currentData().toInt());
    QString extension;
    switch (format) {
        case ImageFormat::PNG: extension = ".png"; break;
        case ImageFormat::JPEG: extension = ".jpg"; break;
        case ImageFormat::BMP: extension = ".bmp"; break;
        case ImageFormat::WEBP: extension = ".webp"; break;
    }
    
    QString path = m_saveDirectory + "/" + filename + extension;
    
    // Save
    bool saved = false;
    switch (format) {
        case ImageFormat::PNG:
            saved = screenshot.save(path, "PNG");
            break;
        case ImageFormat::JPEG:
            saved = screenshot.save(path, "JPG", m_qualitySpin->value());
            break;
        case ImageFormat::BMP:
            saved = screenshot.save(path, "BMP");
            break;
        case ImageFormat::WEBP:
            saved = screenshot.save(path, "WEBP", m_qualitySpin->value());
            break;
    }
    
    if (saved && m_showNotifications) {
        showNotification("Screenshot saved to: " + path);
    }
}

void ScreenshotWindow::onSave() {
    if (!m_lastScreenshot.isNull()) {
        QString path = QFileDialog::getSaveFileName(this, "Save Screenshot",
            m_saveDirectory + "/screenshot.png",
            "PNG Images (*.png);;JPEG Images (*.jpg);;BMP Images (*.bmp);;WEBP Images (*.webp)");
        if (!path.isEmpty()) {
            m_lastScreenshot.save(path);
            if (m_showNotifications) {
                showNotification("Screenshot saved");
            }
        }
    }
}

void ScreenshotWindow::onSaveAs() {
    onSave();
}

void ScreenshotWindow::onCopy() {
    if (!m_lastScreenshot.isNull()) {
        QApplication::clipboard()->setPixmap(m_lastScreenshot);
        if (m_showNotifications) {
            showNotification("Screenshot copied to clipboard");
        }
    }
}

void ScreenshotWindow::onOpenEditor() {
    if (!m_lastScreenshot.isNull()) {
        ScreenshotEditor* editor = new ScreenshotEditor(m_lastScreenshot, this);
        editor->show();
    }
}

void ScreenshotWindow::onChangeFormat(int index) {
    m_imageFormat = static_cast<ImageFormat>(m_formatCombo->itemData(index).toInt());
}

void ScreenshotWindow::onChangeQuality(int quality) {
    // Quality is used for JPEG and WEBP
}

void ScreenshotWindow::onChangeDelay(int delay) {
    m_captureDelay = delay;
}

void ScreenshotWindow::onToggleCursor(bool show) {
    m_includeCursor = show;
}

void ScreenshotWindow::onToggleNotifications(bool enable) {
    m_showNotifications = enable;
}

void ScreenshotWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick) {
        onShowWindow();
    }
}

void ScreenshotWindow::onShowWindow() {
    showNormal();
    activateWindow();
    raise();
}

void ScreenshotWindow::onHideWindow() {
    hide();
}

void ScreenshotWindow::onQuit() {
    saveSettings();
    qApp->quit();
}

void ScreenshotWindow::onAbout() {
    QMessageBox::about(this, "About Screenshot",
        "Havel WM Screenshot\n\n"
        "Screenshot utility for Havel WM.\n\n"
        "Features:\n"
        "- Full screen capture\n"
        "- Active window capture\n"
        "- Region selection\n"
        "- Delayed capture\n"
        "- Built-in editor\n"
        "- Multiple formats (PNG, JPEG, BMP, WEBP)\n"
        "- System tray integration");
}

void ScreenshotWindow::onShortcuts() {
    QMessageBox::information(this, "Keyboard Shortcuts",
        "Ctrl+S    Save screenshot\n"
        "Ctrl+C    Copy to clipboard\n"
        "Ctrl+E    Open editor\n"
        "Ctrl+Q    Quit\n"
        "F1        Show this help");
}

void ScreenshotWindow::showNotification(const QString& message) {
    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage("Screenshot", message);
    }
}

} // namespace havel

#include "Screenshot.moc"
