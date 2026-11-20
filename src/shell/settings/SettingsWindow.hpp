// Settings Application for Havel WM

#pragma once

#include <QMainWindow>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStackedWidget>
#include <QToolBar>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QLineEdit>
#include <QTextEdit>
#include <QTabWidget>
#include <QTreeWidget>
#include <QGroupBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QColorDialog>
#include <QFontDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QSettings>
#include <QTimer>
#include <QKeySequence>
#include <QShortcut>
#include <QProcess>
#include <QPainter>
#include <QStyleFactory>
#include <QPalette>
#include <QFont>
#include <QKeySequence>
#include <QShortcut>
#include <QProcess>

namespace havel {

/**
 * Settings categories
 */
enum class SettingsCategory {
    Appearance,
    Behavior,
    Keybindings,
    Windows,
    Workspace,
    Display,
    Input,
    Network,
    System,
    About
};

/**
 * Settings page widget base
 */
class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr) : QWidget(parent) {}
    virtual void loadSettings() = 0;
    virtual void saveSettings() = 0;
    virtual void resetToDefaults() = 0;
    
signals:
    void settingsChanged();
};

/**
 * Appearance settings page
 */
class AppearanceSettings : public SettingsPage {
    Q_OBJECT

public:
    explicit AppearanceSettings(QWidget* parent = nullptr);
    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;
    
private slots:
    void onThemeChanged(int index);
    void onAccentColorChanged();
    void onFontChanged();
    
private:
    void setupUI();
    
    QComboBox* m_themeCombo;
    QPushButton* m_accentColorButton;
    QPushButton* m_fontButton;
    QSlider* m_opacitySlider;
    QCheckBox* m_animationsCheck;
    QCheckBox* m_transparencyCheck;
    QColor m_accentColor;
};

/**
 * Behavior settings page
 */
class BehaviorSettings : public SettingsPage {
    Q_OBJECT

public:
    explicit BehaviorSettings(QWidget* parent = nullptr);
    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;
    
private:
    void setupUI();
    
    QCheckBox* m_focusFollowsMouseCheck;
    QCheckBox* m_raiseOnFocusCheck;
    QCheckBox* m_clickToFocusCheck;
    QSpinBox* m_snapMarginSpin;
    QComboBox* m_placementCombo;
    QCheckBox* m_centerNewCheck;
    QCheckBox* m_focusNewCheck;
    QCheckBox* m_autoHidePanelCheck;
    QComboBox* m_panelPositionCombo;
};

/**
 * Keybindings settings page
 */
class KeybindingsSettings : public SettingsPage {
    Q_OBJECT

public:
    explicit KeybindingsSettings(QWidget* parent = nullptr);
    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;
    
private slots:
    void onKeybindingClicked(QListWidgetItem* item);
    void onResetKeybinding();
    void onClearKeybinding();
    
private:
    void setupUI();
    void loadDefaultKeybindings();
    
    QTreeWidget* m_keybindingTree;
    QPushButton* m_resetButton;
    QPushButton* m_clearButton;
    
    struct Keybinding {
        QString action;
        QString category;
        QKeySequence sequence;
        QKeySequence defaultSequence;
    };
    QList<Keybinding> m_keybindings;
};

/**
 * Windows settings page
 */
class WindowsSettings : public SettingsPage {
    Q_OBJECT

public:
    explicit WindowsSettings(QWidget* parent = nullptr);
    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;
    
private:
    void setupUI();
    
    QCheckBox* m_titleBarCheck;
    QCheckBox* m_borderCheck;
    QSpinBox* m_borderWidthSpin;
    QComboBox* m_placementCombo;
    QCheckBox* m_centerNewCheck;
    QCheckBox* m_focusNewCheck;
};

/**
 * Workspace settings page
 */
class WorkspaceSettings : public SettingsPage {
    Q_OBJECT

public:
    explicit WorkspaceSettings(QWidget* parent = nullptr);
    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;
    
private:
    void setupUI();
    
    QSpinBox* m_workspaceCountSpin;
    QCheckBox* m_wrapWorkspacesCheck;
    QCheckBox* m_showWorkspaceNamesCheck;
    QLineEdit* m_workspaceNamesEdit;
};

/**
 * Display settings page
 */
class DisplaySettings : public SettingsPage {
    Q_OBJECT

public:
    explicit DisplaySettings(QWidget* parent = nullptr);
    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;
    
private slots:
    void onResolutionChanged(int index);
    void onRefreshRateChanged(int index);
    void onScaleChanged(int index);
    
private:
    void setupUI();
    void detectDisplays();
    
    QComboBox* m_displayCombo;
    QComboBox* m_resolutionCombo;
    QComboBox* m_refreshRateCombo;
    QComboBox* m_scaleCombo;
    QSlider* m_brightnessSlider;
    QSlider* m_gammaSlider;
    QSpinBox* m_temperatureSpin;
};

/**
 * Input settings page
 */
class InputSettings : public SettingsPage {
    Q_OBJECT

public:
    explicit InputSettings(QWidget* parent = nullptr);
    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;
    
private:
    void setupUI();
    
    // Mouse
    QSpinBox* m_mouseSpeedSpin;
    QCheckBox* m_naturalScrollCheck;
    QCheckBox* m_leftHandedCheck;
    
    // Touchpad
    QCheckBox* m_tapToClickCheck;
    QCheckBox* m_disableWhileTypingCheck;
    QCheckBox* m_edgeScrollCheck;
    
    // Keyboard
    QComboBox* m_layoutCombo;
    QSpinBox* m_repeatDelaySpin;
    QSpinBox* m_repeatRateSpin;
};

/**
 * Main Settings Window
 */
class SettingsWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit SettingsWindow(QWidget* parent = nullptr);
    ~SettingsWindow();
    
private slots:
    void onCategoryChanged(QListWidgetItem* current, QListWidgetItem* previous);
    void onApply();
    void onReset();
    void onOK();
    void onCancel();
    void onSettingsChanged();
    
private:
    void setupUI();
    void setupCategories();
    void loadAllSettings();
    void saveAllSettings();
    
    QWidget* m_centralWidget;
    QVBoxLayout* m_mainLayout;
    
    // Left panel - categories
    QListWidget* m_categoryList;
    
    // Right panel - settings pages
    QStackedWidget* m_pagesStack;
    
    // Settings pages
    AppearanceSettings* m_appearancePage;
    BehaviorSettings* m_behaviorPage;
    KeybindingsSettings* m_keybindingsPage;
    WindowsSettings* m_windowsPage;
    WorkspaceSettings* m_workspacePage;
    DisplaySettings* m_displayPage;
    InputSettings* m_inputPage;
    
    // Bottom buttons
    QHBoxLayout* m_buttonLayout;
    QPushButton* m_applyButton;
    QPushButton* m_resetButton;
    QPushButton* m_okButton;
    QPushButton* m_cancelButton;
    
    // State
    bool m_modified;
};

} // namespace havel
