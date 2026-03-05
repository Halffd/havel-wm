// File Properties Dialog

#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QListWidget>
#include <QDateTime>
#include <QFileInfo>

namespace havel {

/**
 * File Properties Dialog
 */
class FilePropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit FilePropertiesDialog(const QString& filePath, QWidget* parent = nullptr);
    
private slots:
    void onNameChanged(const QString& text);
    void onOpenWithSelected(int index);
    void onChooseAppClicked();
    void onPermissionsChanged();
    void applyChanges();

private:
    void setupBasicTab();
    void setupOpenWithTab();
    void setupPermissionsTab();
    void setupAssociationsTab();
    void updateFileSize();
    void updatePermissions();
    QString formatFileSize(qint64 size);
    QString formatDateTime(const QDateTime& dt);
    
    QString m_filePath;
    QFileInfo m_fileInfo;
    
    QTabWidget* m_tabWidget;
    
    // Basic tab
    QLabel* m_iconLabel;
    QLineEdit* m_nameEdit;
    QLabel* m_typeLabel;
    QLabel* m_sizeLabel;
    QLabel* m_modifiedLabel;
    QLabel* m_accessedLabel;
    QLabel* m_createdLabel;
    QLabel* m_pathLabel;
    
    // Open With tab
    QListWidget* m_appList;
    QPushButton* m_chooseAppButton;
    
    // Permissions tab
    QCheckBox* m_ownerRead;
    QCheckBox* m_ownerWrite;
    QCheckBox* m_ownerExec;
    QCheckBox* m_groupRead;
    QCheckBox* m_groupWrite;
    QCheckBox* m_groupExec;
    QCheckBox* m_otherRead;
    QCheckBox* m_otherWrite;
    QCheckBox* m_otherExec;
    QCheckBox* m_setuid;
    QCheckBox* m_setgid;
    
    QPushButton* m_okButton;
    QPushButton* m_cancelButton;
    QPushButton* m_applyButton;
};

/**
 * File Picker Dialog
 * 
 * Modal dialog for selecting files/directories
 */
class FilePickerDialog : public QDialog {
    Q_OBJECT

public:
    enum class Mode {
        OpenFile,
        OpenDirectory,
        SaveFile,
        MultipleFiles
    };
    
    explicit FilePickerDialog(QWidget* parent = nullptr,
                              Mode mode = Mode::OpenFile,
                              const QString& startPath = "");
    
    // Get selected file(s)
    QString selectedFile() const;
    QStringList selectedFiles() const;
    
    // Set filters
    void setNameFilters(const QStringList& filters);
    void setFileMode(int mode);
    void setAcceptMode(int mode);
    
private slots:
    void onDirectoryClicked(const QModelIndex& index);
    void onFileDoubleClicked(const QModelIndex& index);
    void onSelectionChanged();
    void onOkClicked();
    void onCancelClicked();
    void onUpClicked();
    void onHomeClicked();
    void onRefreshClicked();
    void onNewFolderClicked();
    void onFilterChanged(const QString& text);

private:
    void setupUI();
    void updateFileList();
    void updatePathBar();
    bool acceptFile(const QFileInfo& fi) const;
    
    Mode m_mode;
    QString m_currentPath;
    QString m_startPath;
    QStringList m_nameFilters;
    
    // UI components
    QLineEdit* m_pathBar;
    QLineEdit* m_filterBar;
    QLineEdit* m_nameEdit;
    QListWidget* m_fileList;
    
    QPushButton* m_okButton;
    QPushButton* m_cancelButton;
    QPushButton* m_upButton;
    QPushButton* m_homeButton;
    QPushButton* m_refreshButton;
    QPushButton* m_newFolderButton;
    
    QLabel* m_statusLabel;
    
    QString m_selectedFile;
    QStringList m_selectedFiles;
};

} // namespace havel
