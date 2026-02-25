#include "SettingsWindow.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QGroupBox>
#include <QSettings>

namespace havel {

SettingsWindow::SettingsWindow(QWidget* parent)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
    , m_tabs(new QTabWidget(this))
    , m_enableTiling(new QCheckBox("Enable tiling by default", this))
    , m_enableAnimations(new QCheckBox("Enable animations", this))
    , m_gapSize(new QSpinBox(this))
    , m_terminalCombo(new QComboBox(this))
    , m_themeCombo(new QComboBox(this))
    , m_borderWidth(new QSpinBox(this))
{
    setWindowTitle("Havel WM Settings");
    setMinimumSize(500, 400);
    
    setupUI();
    loadSettings();
}

void SettingsWindow::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs);
    
    // Create tabs
    setupGeneralTab();
    setupAppearanceTab();
    // Keybindings tab would be added here
    
    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    auto* resetButton = new QPushButton("Reset to Defaults");
    connect(resetButton, &QPushButton::clicked, this, &SettingsWindow::onReset);
    buttonLayout->addWidget(resetButton);
    
    auto* applyButton = new QPushButton("Apply");
    connect(applyButton, &QPushButton::clicked, this, &SettingsWindow::onApply);
    buttonLayout->addWidget(applyButton);
    
    auto* closeButton = new QPushButton("Close");
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(closeButton);
    
    layout->addLayout(buttonLayout);
}

void SettingsWindow::setupGeneralTab() {
    auto* tab = new QWidget();
    auto* layout = new QFormLayout(tab);
    
    // Tiling
    m_enableTiling->setChecked(true);
    layout->addRow("Default tiling:", m_enableTiling);
    
    // Animations
    m_enableAnimations->setChecked(true);
    layout->addRow("Animations:", m_enableAnimations);
    
    // Gap size
    m_gapSize->setRange(0, 50);
    m_gapSize->setValue(10);
    m_gapSize->setSuffix(" px");
    layout->addRow("Window gaps:", m_gapSize);
    
    // Terminal
    m_terminalCombo->addItem("foot", "foot");
    m_terminalCombo->addItem("Alacritty", "alacritty");
    m_terminalCombo->addItem("Kitty", "kitty");
    m_terminalCombo->addItem("WezTerm", "wezterm");
    layout->addRow("Terminal:", m_terminalCombo);
    
    m_tabs->addTab(tab, "General");
}

void SettingsWindow::setupAppearanceTab() {
    auto* tab = new QWidget();
    auto* layout = new QFormLayout(tab);
    
    // Theme
    m_themeCombo->addItem("Dark", "dark");
    m_themeCombo->addItem("Light", "light");
    m_themeCombo->addItem("System", "system");
    layout->addRow("Theme:", m_themeCombo);
    
    // Border width
    m_borderWidth->setRange(1, 10);
    m_borderWidth->setValue(2);
    m_borderWidth->setSuffix(" px");
    layout->addRow("Border width:", m_borderWidth);
    
    m_tabs->addTab(tab, "Appearance");
}

void SettingsWindow::loadSettings() {
    QSettings settings("Havel", "HavelWM");
    
    m_enableTiling->setChecked(settings.value("tiling/enabled", true).toBool());
    m_enableAnimations->setChecked(settings.value("animations/enabled", true).toBool());
    m_gapSize->setValue(settings.value("tiling/gapSize", 10).toInt());
    m_terminalCombo->setCurrentText(settings.value("terminal", "foot").toString());
    m_themeCombo->setCurrentText(settings.value("appearance/theme", "dark").toString());
    m_borderWidth->setValue(settings.value("appearance/borderWidth", 2).toInt());
}

void SettingsWindow::saveSettings() {
    QSettings settings("Havel", "HavelWM");
    
    settings.setValue("tiling/enabled", m_enableTiling->isChecked());
    settings.setValue("animations/enabled", m_enableAnimations->isChecked());
    settings.setValue("tiling/gapSize", m_gapSize->value());
    settings.setValue("terminal", m_terminalCombo->currentData().toString());
    settings.setValue("appearance/theme", m_themeCombo->currentData().toString());
    settings.setValue("appearance/borderWidth", m_borderWidth->value());
    
    settings.sync();
    
    emit settingsChanged();
}

void SettingsWindow::onApply() {
    saveSettings();
}

void SettingsWindow::onReset() {
    QSettings settings("Havel", "HavelWM");
    settings.clear();
    settings.sync();
    loadSettings();
    emit settingsChanged();
}

} // namespace havel
