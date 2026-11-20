// Settings Application Implementation

#include "SettingsWindow.hpp"
#include <QApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDateTime>
#include <QKeySequenceEdit>

namespace havel {

// ============================================================================
// AppearanceSettings Implementation
// ============================================================================

AppearanceSettings::AppearanceSettings(QWidget* parent)
    : SettingsPage(parent)
{
    setupUI();
}

void AppearanceSettings::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    
    QGroupBox* themeGroup = new QGroupBox("Theme");
    QFormLayout* themeLayout = new QFormLayout(themeGroup);
    
    m_themeCombo = new QComboBox();
    m_themeCombo->addItem("Dark", "dark");
    m_themeCombo->addItem("Light", "light");
    m_themeCombo->addItem("System", "system");
    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AppearanceSettings::onThemeChanged);
    themeLayout->addRow("Theme:", m_themeCombo);
    
    m_accentColorButton = new QPushButton();
    m_accentColorButton->setFixedSize(100, 30);
    connect(m_accentColorButton, &QPushButton::clicked,
            this, &AppearanceSettings::onAccentColorChanged);
    themeLayout->addRow("Accent Color:", m_accentColorButton);
    
    m_fontButton = new QPushButton("Change Font...");
    connect(m_fontButton, &QPushButton::clicked, this, &AppearanceSettings::onFontChanged);
    themeLayout->addRow("Application Font:", m_fontButton);
    
    layout->addWidget(themeGroup);
    
    QGroupBox* effectsGroup = new QGroupBox("Visual Effects");
    QFormLayout* effectsLayout = new QFormLayout(effectsGroup);
    
    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(50, 100);
    m_opacitySlider->setValue(95);
    effectsLayout->addRow("Window Opacity:", m_opacitySlider);
    
    m_animationsCheck = new QCheckBox("Enable animations");
    m_animationsCheck->setChecked(true);
    effectsLayout->addRow(m_animationsCheck);
    
    m_transparencyCheck = new QCheckBox("Enable transparency");
    m_transparencyCheck->setChecked(true);
    effectsLayout->addRow(m_transparencyCheck);
    
    layout->addWidget(effectsGroup);
    layout->addStretch();
}

void AppearanceSettings::loadSettings() {
    QSettings settings("Havel WM", "Settings");
    
    QString theme = settings.value("appearance/theme", "dark").toString();
    int themeIndex = m_themeCombo->findData(theme);
    if (themeIndex >= 0) m_themeCombo->setCurrentIndex(themeIndex);
    
    QString accent = settings.value("appearance/accentColor", "#4CAF50").toString();
    m_accentColor = QColor(accent);
    m_accentColorButton->setStyleSheet(
        QString("background-color: %1; border: 1px solid gray;").arg(accent));
    
    int opacity = settings.value("appearance/opacity", 95).toInt();
    m_opacitySlider->setValue(opacity);
    
    bool animations = settings.value("appearance/animations", true).toBool();
    m_animationsCheck->setChecked(animations);
    
    bool transparency = settings.value("appearance/transparency", true).toBool();
    m_transparencyCheck->setChecked(transparency);
}

void AppearanceSettings::saveSettings() {
    QSettings settings("Havel WM", "Settings");
    
    settings.setValue("appearance/theme", m_themeCombo->currentData());
    settings.setValue("appearance/accentColor", m_accentColor.name());
    settings.setValue("appearance/opacity", m_opacitySlider->value());
    settings.setValue("appearance/animations", m_animationsCheck->isChecked());
    settings.setValue("appearance/transparency", m_transparencyCheck->isChecked());
}

void AppearanceSettings::resetToDefaults() {
    m_themeCombo->setCurrentIndex(0);  // Dark
    m_accentColor = QColor("#4CAF50");
    m_accentColorButton->setStyleSheet(
        "background-color: #4CAF50; border: 1px solid gray;");
    m_opacitySlider->setValue(95);
    m_animationsCheck->setChecked(true);
    m_transparencyCheck->setChecked(true);
    emit settingsChanged();
}

void AppearanceSettings::onThemeChanged(int index) {
    QString theme = m_themeCombo->itemData(index).toString();
    
    if (theme == "dark") {
        QApplication::setStyle(QStyleFactory::create("Fusion"));
        QPalette darkPalette;
        darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
        darkPalette.setColor(QPalette::Text, Qt::white);
        QApplication::setPalette(darkPalette);
    } else if (theme == "light") {
        QApplication::setStyle(QStyleFactory::create("Fusion"));
        QPalette lightPalette;
        lightPalette.setColor(QPalette::Window, QColor(240, 240, 240));
        lightPalette.setColor(QPalette::WindowText, Qt::black);
        lightPalette.setColor(QPalette::Base, Qt::white);
        lightPalette.setColor(QPalette::Text, Qt::black);
        QApplication::setPalette(lightPalette);
    }
    
    emit settingsChanged();
}

void AppearanceSettings::onAccentColorChanged() {
    QColor color = QColorDialog::getColor(m_accentColor, this, "Select Accent Color");
    if (color.isValid()) {
        m_accentColor = color;
        m_accentColorButton->setStyleSheet(
            QString("background-color: %1; border: 1px solid gray;").arg(color.name()));
        emit settingsChanged();
    }
}

void AppearanceSettings::onFontChanged() {
    bool ok;
    QFont font = QFontDialog::getFont(&ok, QApplication::font(), this, "Select Font");
    if (ok) {
        QApplication::setFont(font);
        emit settingsChanged();
    }
}

// ============================================================================
// BehaviorSettings Implementation
// ============================================================================

BehaviorSettings::BehaviorSettings(QWidget* parent)
    : SettingsPage(parent)
{
    setupUI();
}

void BehaviorSettings::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    
    QGroupBox* focusGroup = new QGroupBox("Window Focus");
    QFormLayout* focusLayout = new QFormLayout(focusGroup);
    
    m_focusFollowsMouseCheck = new QCheckBox("Focus follows mouse");
    focusLayout->addRow(m_focusFollowsMouseCheck);
    
    m_raiseOnFocusCheck = new QCheckBox("Raise window on focus");
    m_raiseOnFocusCheck->setChecked(true);
    focusLayout->addRow(m_raiseOnFocusCheck);
    
    m_clickToFocusCheck = new QCheckBox("Click to focus");
    m_clickToFocusCheck->setChecked(true);
    focusLayout->addRow(m_clickToFocusCheck);
    
    layout->addWidget(focusGroup);
    
    QGroupBox* placementGroup = new QGroupBox("Window Placement");
    QFormLayout* placementLayout = new QFormLayout(placementGroup);
    
    m_snapMarginSpin = new QSpinBox();
    m_snapMarginSpin->setRange(0, 100);
    m_snapMarginSpin->setValue(10);
    m_snapMarginSpin->setSuffix(" px");
    placementLayout->addRow("Snap margin:", m_snapMarginSpin);
    
    m_placementCombo = new QComboBox();
    m_placementCombo->addItem("Smart");
    m_placementCombo->addItem("Cascade");
    m_placementCombo->addItem("Center");
    placementLayout->addRow("New window placement:", m_placementCombo);
    
    m_centerNewCheck = new QCheckBox("Center new windows");
    placementLayout->addRow(m_centerNewCheck);
    
    m_focusNewCheck = new QCheckBox("Focus new windows");
    m_focusNewCheck->setChecked(true);
    placementLayout->addRow(m_focusNewCheck);
    
    layout->addWidget(placementGroup);
    
    QGroupBox* panelGroup = new QGroupBox("Panel");
    QFormLayout* panelLayout = new QFormLayout(panelGroup);
    
    m_autoHidePanelCheck = new QCheckBox("Auto-hide panel");
    panelLayout->addRow(m_autoHidePanelCheck);
    
    m_panelPositionCombo = new QComboBox();
    m_panelPositionCombo->addItem("Top");
    m_panelPositionCombo->addItem("Bottom");
    panelLayout->addRow("Panel position:", m_panelPositionCombo);
    
    layout->addWidget(panelGroup);
    layout->addStretch();
}

void BehaviorSettings::loadSettings() {
    QSettings settings("Havel WM", "Settings");
    
    m_focusFollowsMouseCheck->setChecked(
        settings.value("behavior/focusFollowsMouse", false).toBool());
    m_raiseOnFocusCheck->setChecked(
        settings.value("behavior/raiseOnFocus", true).toBool());
    m_clickToFocusCheck->setChecked(
        settings.value("behavior/clickToFocus", true).toBool());
    m_snapMarginSpin->setValue(
        settings.value("behavior/snapMargin", 10).toInt());
    m_placementCombo->setCurrentIndex(
        settings.value("behavior/placement", 0).toInt());
    m_centerNewCheck->setChecked(
        settings.value("behavior/centerNew", false).toBool());
    m_focusNewCheck->setChecked(
        settings.value("behavior/focusNew", true).toBool());
    m_autoHidePanelCheck->setChecked(
        settings.value("behavior/autoHidePanel", false).toBool());
    m_panelPositionCombo->setCurrentIndex(
        settings.value("behavior/panelPosition", 1).toInt());
}

void BehaviorSettings::saveSettings() {
    QSettings settings("Havel WM", "Settings");
    
    settings.setValue("behavior/focusFollowsMouse", m_focusFollowsMouseCheck->isChecked());
    settings.setValue("behavior/raiseOnFocus", m_raiseOnFocusCheck->isChecked());
    settings.setValue("behavior/clickToFocus", m_clickToFocusCheck->isChecked());
    settings.setValue("behavior/snapMargin", m_snapMarginSpin->value());
    settings.setValue("behavior/placement", m_placementCombo->currentIndex());
    settings.setValue("behavior/centerNew", m_centerNewCheck->isChecked());
    settings.setValue("behavior/focusNew", m_focusNewCheck->isChecked());
    settings.setValue("behavior/autoHidePanel", m_autoHidePanelCheck->isChecked());
    settings.setValue("behavior/panelPosition", m_panelPositionCombo->currentIndex());
}

void BehaviorSettings::resetToDefaults() {
    m_focusFollowsMouseCheck->setChecked(false);
    m_raiseOnFocusCheck->setChecked(true);
    m_clickToFocusCheck->setChecked(true);
    m_snapMarginSpin->setValue(10);
    m_placementCombo->setCurrentIndex(0);
    m_centerNewCheck->setChecked(false);
    m_focusNewCheck->setChecked(true);
    m_autoHidePanelCheck->setChecked(false);
    m_panelPositionCombo->setCurrentIndex(1);
    emit settingsChanged();
}

// ============================================================================
// SettingsWindow Implementation
// ============================================================================

SettingsWindow::SettingsWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_modified(false)
{
    setWindowTitle("Settings - Havel WM");
    setMinimumSize(800, 600);
    resize(900, 650);
    
    setupUI();
    setupCategories();
    loadAllSettings();
}

SettingsWindow::~SettingsWindow() {
    saveAllSettings();
}

void SettingsWindow::setupUI() {
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    m_mainLayout = new QVBoxLayout(m_centralWidget);
    
    // Horizontal layout for categories and pages
    QHBoxLayout* contentLayout = new QHBoxLayout();
    
    // Category list
    m_categoryList = new QListWidget();
    m_categoryList->setMaximumWidth(200);
    m_categoryList->setViewMode(QListView::ListMode);
    connect(m_categoryList, &QListWidget::currentItemChanged,
            this, &SettingsWindow::onCategoryChanged);
    contentLayout->addWidget(m_categoryList);
    
    // Settings pages
    m_pagesStack = new QStackedWidget();
    contentLayout->addWidget(m_pagesStack, 1);
    
    m_mainLayout->addLayout(contentLayout);
    
    // Bottom buttons
    m_buttonLayout = new QHBoxLayout();
    m_buttonLayout->addStretch();
    
    m_resetButton = new QPushButton("Reset");
    connect(m_resetButton, &QPushButton::clicked, this, &SettingsWindow::onReset);
    m_buttonLayout->addWidget(m_resetButton);
    
    m_applyButton = new QPushButton("Apply");
    connect(m_applyButton, &QPushButton::clicked, this, &SettingsWindow::onApply);
    m_buttonLayout->addWidget(m_applyButton);
    
    m_cancelButton = new QPushButton("Cancel");
    connect(m_cancelButton, &QPushButton::clicked, this, &SettingsWindow::onCancel);
    m_buttonLayout->addWidget(m_cancelButton);
    
    m_okButton = new QPushButton("OK");
    connect(m_okButton, &QPushButton::clicked, this, &SettingsWindow::onOK);
    m_buttonLayout->addWidget(m_okButton);
    
    m_mainLayout->addLayout(m_buttonLayout);
}

void SettingsWindow::setupCategories() {
    m_appearancePage = new AppearanceSettings(this);
    m_behaviorPage = new BehaviorSettings(this);
    m_keybindingsPage = new KeybindingsSettings(this);
    m_windowsPage = new WindowsSettings(this);
    m_workspacePage = new WorkspaceSettings(this);
    m_displayPage = new DisplaySettings(this);
    m_inputPage = new InputSettings(this);
    
    m_pagesStack->addWidget(m_appearancePage);
    m_pagesStack->addWidget(m_behaviorPage);
    m_pagesStack->addWidget(m_keybindingsPage);
    m_pagesStack->addWidget(m_windowsPage);
    m_pagesStack->addWidget(m_workspacePage);
    m_pagesStack->addWidget(m_displayPage);
    m_pagesStack->addWidget(m_inputPage);
    
    // Connect settings changed signals
    connect(m_appearancePage, &SettingsPage::settingsChanged, this, &SettingsWindow::onSettingsChanged);
    connect(m_behaviorPage, &SettingsPage::settingsChanged, this, &SettingsWindow::onSettingsChanged);
    
    // Category items
    QStringList categories = {
        "🎨 Appearance",
        "⚙️ Behavior",
        "⌨️ Keybindings",
        "🪟 Windows",
        "🖥️ Workspace",
        "🖥️ Display",
        "🖱️ Input"
    };
    
    for (const QString& cat : categories) {
        QListWidgetItem* item = new QListWidgetItem(cat, m_categoryList);
    }
    
    m_categoryList->setCurrentRow(0);
}

void SettingsWindow::onCategoryChanged(QListWidgetItem* current, QListWidgetItem* previous) {
    if (!current) return;
    
    int index = m_categoryList->row(current);
    m_pagesStack->setCurrentIndex(index);
}

void SettingsWindow::loadAllSettings() {
    m_appearancePage->loadSettings();
    m_behaviorPage->loadSettings();
    m_keybindingsPage->loadSettings();
    m_windowsPage->loadSettings();
    m_workspacePage->loadSettings();
    m_displayPage->loadSettings();
    m_inputPage->loadSettings();
    
    m_modified = false;
}

void SettingsWindow::saveAllSettings() {
    m_appearancePage->saveSettings();
    m_behaviorPage->saveSettings();
    m_keybindingsPage->saveSettings();
    m_windowsPage->saveSettings();
    m_workspacePage->saveSettings();
    m_displayPage->saveSettings();
    m_inputPage->saveSettings();
    
    m_modified = false;
}

void SettingsWindow::onApply() {
    saveAllSettings();
}

void SettingsWindow::onReset() {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Reset Settings", "Reset all settings to defaults?",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        m_appearancePage->resetToDefaults();
        m_behaviorPage->resetToDefaults();
        m_keybindingsPage->resetToDefaults();
        m_windowsPage->resetToDefaults();
        m_workspacePage->resetToDefaults();
        m_displayPage->resetToDefaults();
        m_inputPage->resetToDefaults();
        m_modified = true;
    }
}

void SettingsWindow::onOK() {
    saveAllSettings();
    close();
}

void SettingsWindow::onCancel() {
    if (m_modified) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "Unsaved Changes", "Discard unsaved changes?",
            QMessageBox::Yes | QMessageBox::No);
        
        if (reply == QMessageBox::Yes) {
            close();
        }
    } else {
        close();
    }
}

void SettingsWindow::onSettingsChanged() {
    m_modified = true;
}

// Stub implementations for other pages - with helpful messages
KeybindingsSettings::KeybindingsSettings(QWidget* parent) : SettingsPage(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    
    QLabel* infoLabel = new QLabel(
        "Keybindings are configured in the compositor.\n\n"
        "Default keybindings:\n"
        "  • Super+Enter - Open terminal\n"
        "  • Super+D - Open app launcher\n"
        "  • Super+Q - Close focused window\n"
        "  • Super+Left/Right - Snap window\n"
        "  • Super+1-9 - Switch workspace\n\n"
        "Configuration file: ~/.config/havel-wm/config.json");
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);
    layout->addStretch();
}
void KeybindingsSettings::loadSettings() {}
void KeybindingsSettings::saveSettings() {}
void KeybindingsSettings::resetToDefaults() {}
void KeybindingsSettings::onKeybindingClicked(QListWidgetItem*) {}
void KeybindingsSettings::onResetKeybinding() {}
void KeybindingsSettings::onClearKeybinding() {}

WindowsSettings::WindowsSettings(QWidget* parent) : SettingsPage(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    
    QLabel* infoLabel = new QLabel(
        "Window behavior is managed by the compositor.\n\n"
        "Features:\n"
        "  • Drag windows by title bar\n"
        "  • Resize from edges\n"
        "  • Maximize/Fullscreen buttons\n"
        "  • Auto-snap to edges\n"
        "  • Multi-workspace support");
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);
    layout->addStretch();
}
void WindowsSettings::loadSettings() {}
void WindowsSettings::saveSettings() {}
void WindowsSettings::resetToDefaults() {}

WorkspaceSettings::WorkspaceSettings(QWidget* parent) : SettingsPage(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    
    QLabel* infoLabel = new QLabel(
        "Workspaces are managed by the compositor.\n\n"
        "Usage:\n"
        "  • Super+1-9 - Switch workspace\n"
        "  • Windows can be moved between workspaces\n"
        "  • Each workspace has independent window layout\n"
        "  • Workspace indicator shown in panel");
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);
    layout->addStretch();
}
void WorkspaceSettings::loadSettings() {}
void WorkspaceSettings::saveSettings() {}
void WorkspaceSettings::resetToDefaults() {}

DisplaySettings::DisplaySettings(QWidget* parent) : SettingsPage(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    
    QLabel* infoLabel = new QLabel(
        "Display settings are managed by wlroots.\n\n"
        "Current displays:\n"
        "  • Use wlr-randr command line tool:\n"
        "    wlr-randr --output HDMI-A-1 --mode 1920x1080@60\n\n"
        "  • Or create ~/.config/wlr-randr/config");
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);
    layout->addStretch();
}
void DisplaySettings::loadSettings() {}
void DisplaySettings::saveSettings() {}
void DisplaySettings::resetToDefaults() {}
void DisplaySettings::onResolutionChanged(int) {}
void DisplaySettings::onRefreshRateChanged(int) {}
void DisplaySettings::onScaleChanged(int) {}

InputSettings::InputSettings(QWidget* parent) : SettingsPage(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    
    QLabel* infoLabel = new QLabel(
        "Input settings are managed by wlroots.\n\n"
        "Configuration file: ~/.config/havel-wm/input.json\n\n"
        "Options:\n"
        "  • Keyboard layout\n"
        "  • Mouse acceleration\n"
        "  • Touchpad natural scrolling\n"
        "  • Tap-to-click");
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);
    layout->addStretch();
}
void InputSettings::loadSettings() {}
void InputSettings::saveSettings() {}
void InputSettings::resetToDefaults() {}

} // namespace havel

#include "SettingsWindow.moc"
