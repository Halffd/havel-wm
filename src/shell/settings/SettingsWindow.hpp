#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QTabWidget>
#include <QFormLayout>

namespace havel {

/**
 * Settings window for compositor configuration
 */
class SettingsWindow : public QDialog {
    Q_OBJECT

public:
    explicit SettingsWindow(QWidget* parent = nullptr);
    
    // Load current settings
    void loadSettings();
    
    // Save settings
    void saveSettings();

signals:
    void settingsChanged();

private slots:
    void onApply();
    void onReset();

private:
    void setupUI();
    void setupGeneralTab();
    void setupKeybindingsTab();
    void setupAppearanceTab();
    
    QTabWidget* m_tabs;
    
    // General settings
    QCheckBox* m_enableTiling;
    QCheckBox* m_enableAnimations;
    QSpinBox* m_gapSize;
    QComboBox* m_terminalCombo;
    
    // Keybindings would be added here
    
    // Appearance
    QComboBox* m_themeCombo;
    QSpinBox* m_borderWidth;
};

} // namespace havel
