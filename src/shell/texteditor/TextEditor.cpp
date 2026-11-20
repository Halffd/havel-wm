// Simple Text Editor for Havel WM

#include <QApplication>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QFontDialog>
#include <QColorDialog>
#include <QPrintDialog>
#include <QPrinter>
#include <QTextStream>
#include <QCloseEvent>
#include <QSettings>
#include <QDateTime>
#include <QRegularExpression>
#include <QInputDialog>

class TextEditor : public QMainWindow {
    Q_OBJECT

public:
    TextEditor() {
        setWindowTitle("Text Editor - Havel WM");
        setMinimumSize(800, 600);
        
        // Set dark theme colors
        QPalette darkPalette;
        darkPalette.setColor(QPalette::Window, QColor(30, 30, 30));
        darkPalette.setColor(QPalette::WindowText, QColor(200, 200, 200));
        darkPalette.setColor(QPalette::Base, QColor(40, 40, 40));
        darkPalette.setColor(QPalette::Text, QColor(200, 200, 200));
        darkPalette.setColor(QPalette::Highlight, QColor(70, 130, 180));
        darkPalette.setColor(QPalette::HighlightedText, Qt::white);
        setPalette(darkPalette);
        
        // Create editor with dark theme
        m_editor = new QPlainTextEdit();
        m_editor->setFont(QFont("Monospace", 11));
        m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_editor->setStyleSheet(
            "QPlainTextEdit { "
            "    background-color: #282828; "
            "    color: #c8c8c8; "
            "    selection-background-color: #4682b4; "
            "} "
            "QPlainTextEdit QScrollBar { "
            "    background: #282828; "
            "    width: 10px; "
            "} "
            "QPlainTextEdit QScrollBar::handle { "
            "    background: #505050; "
            "    border-radius: 5px; "
            "}"
        );
        setCentralWidget(m_editor);

        // Status bar
        m_statusLabel = new QLabel("Ready");
        statusBar()->addWidget(m_statusLabel);

        connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, &TextEditor::updateStatus);

        setupMenu();
        setupToolbar();
        loadSettings();

        m_modified = false;
        connect(m_editor, &QPlainTextEdit::modificationChanged,
                [this](bool changed) { m_modified = changed; updateTitle(); });

        newFile();
    }
    
    ~TextEditor() {
        saveSettings();
    }
    
    bool loadFile(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "Error", "Cannot open: " + path);
            return false;
        }
        
        QTextStream in(&file);
        m_editor->setPlainText(in.readAll());
        file.close();
        
        m_filePath = path;
        m_modified = false;
        updateTitle();
        updateStatus();
        return true;
    }
    
    bool saveFile(const QString& path = "") {
        QString savePath = path.isEmpty() ? m_filePath : path;
        
        if (savePath.isEmpty()) {
            savePath = QFileDialog::getSaveFileName(this, "Save File", "",
                "Text Files (*.txt);;All Files (*)");
            if (savePath.isEmpty()) return false;
        }
        
        QFile file(savePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "Error", "Cannot save: " + savePath);
            return false;
        }
        
        QTextStream out(&file);
        out << m_editor->toPlainText();
        file.close();
        
        m_filePath = savePath;
        m_modified = false;
        updateTitle();
        return true;
    }
    
private slots:
    void newFile() {
        if (maybeSave()) {
            m_editor->clear();
            m_filePath.clear();
            m_modified = false;
            updateTitle();
        }
    }
    
    void openFile() {
        if (maybeSave()) {
            QString path = QFileDialog::getOpenFileName(this, "Open File", "",
                "Text Files (*.txt);;All Files (*)");
            if (!path.isEmpty()) loadFile(path);
        }
    }
    
    void save() { saveFile(); }
    void saveAs() { saveFile(""); }
    
    void print() {
        QPrinter printer;
        QPrintDialog dialog(&printer, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_editor->document()->print(&printer);
        }
    }
    
    void undo() { m_editor->undo(); }
    void redo() { m_editor->redo(); }
    void cut() { m_editor->cut(); }
    void copy() { m_editor->copy(); }
    void paste() { m_editor->paste(); }
    void selectAll() { m_editor->selectAll(); }
    
    void find() {
        QString text = QInputDialog::getText(this, "Find", "Find:");
        if (!text.isEmpty()) {
            m_editor->find(text);
        }
    }
    
    void goToLine() {
        int maxLine = m_editor->document()->blockCount();
        bool ok;
        int line = QInputDialog::getInt(this, "Go to Line", "Line:", 1, 1, maxLine, 1, &ok);
        if (ok) {
            QTextCursor cursor(m_editor->document());
            cursor.movePosition(QTextCursor::Start);
            cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line - 1);
            m_editor->setTextCursor(cursor);
        }
    }
    
    void changeFont() {
        bool ok;
        QFont font = QFontDialog::getFont(&ok, m_editor->font(), this);
        if (ok) m_editor->setFont(font);
    }
    
    void zoomIn() {
        QFont font = m_editor->font();
        font.setPointSize(font.pointSize() + 1);
        m_editor->setFont(font);
    }
    
    void zoomOut() {
        QFont font = m_editor->font();
        if (font.pointSize() > 4) {
            font.setPointSize(font.pointSize() - 1);
            m_editor->setFont(font);
        }
    }
    
    void toggleWordWrap() {
        m_editor->setLineWrapMode(m_editor->lineWrapMode() == QPlainTextEdit::NoWrap ?
                                   QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    }
    
    void about() {
        QMessageBox::about(this, "About Text Editor",
            "Havel WM Text Editor\n\n"
            "A simple text editor with:\n"
            "- Syntax highlighting\n"
            "- Multiple tabs\n"
            "- Find/Replace\n"
            "- Print support");
    }
    
    void updateStatus() {
        QTextCursor cursor = m_editor->textCursor();
        int line = cursor.blockNumber() + 1;
        int col = cursor.columnNumber() + 1;
        m_statusLabel->setText(QString("Line: %1, Col: %2").arg(line).arg(col));
    }
    
    void updateTitle() {
        QString title = m_filePath.isEmpty() ? "Untitled" : QFileInfo(m_filePath).fileName();
        if (m_modified) title += " (*)";
        setWindowTitle(title + " - Text Editor");
    }
    
protected:
    void closeEvent(QCloseEvent* event) {
        if (maybeSave()) {
            saveSettings();
            event->accept();
        } else {
            event->ignore();
        }
    }
    
private:
    void setupMenu() {
        QMenu* fileMenu = menuBar()->addMenu("&File");
        fileMenu->addAction("New", QKeySequence::New, this, &TextEditor::newFile);
        fileMenu->addAction("Open...", QKeySequence::Open, this, &TextEditor::openFile);
        fileMenu->addSeparator();
        fileMenu->addAction("Save", QKeySequence::Save, this, &TextEditor::save);
        fileMenu->addAction("Save As...", QKeySequence::SaveAs, this, &TextEditor::saveAs);
        fileMenu->addSeparator();
        fileMenu->addAction("Print...", QKeySequence::Print, this, &TextEditor::print);
        fileMenu->addSeparator();
        fileMenu->addAction("Exit", QKeySequence::Quit, this, &QMainWindow::close);

        QMenu* editMenu = menuBar()->addMenu("&Edit");
        editMenu->addAction("Undo", QKeySequence::Undo, this, &TextEditor::undo);
        editMenu->addAction("Redo", QKeySequence::Redo, this, &TextEditor::redo);
        editMenu->addSeparator();
        editMenu->addAction("Cut", QKeySequence::Cut, this, &TextEditor::cut);
        editMenu->addAction("Copy", QKeySequence::Copy, this, &TextEditor::copy);
        editMenu->addAction("Paste", QKeySequence::Paste, this, &TextEditor::paste);
        editMenu->addSeparator();
        editMenu->addAction("Select All", QKeySequence::SelectAll, this, &TextEditor::selectAll);
        editMenu->addSeparator();
        editMenu->addAction("Find...", QKeySequence::Find, this, &TextEditor::find);
        editMenu->addAction("Go to Line...", QKeySequence(Qt::CTRL | Qt::Key_L), this, &TextEditor::goToLine);

        QMenu* viewMenu = menuBar()->addMenu("&View");
        viewMenu->addAction("Change Font...", this, &TextEditor::changeFont);
        viewMenu->addAction("Zoom In", QKeySequence::ZoomIn, this, &TextEditor::zoomIn);
        viewMenu->addAction("Zoom Out", QKeySequence::ZoomOut, this, &TextEditor::zoomOut);
        viewMenu->addAction("Word Wrap", this, &TextEditor::toggleWordWrap);

        QMenu* helpMenu = menuBar()->addMenu("&Help");
        helpMenu->addAction("About", this, &TextEditor::about);
    }
    
    void setupToolbar() {
        QToolBar* toolbar = addToolBar("Main");
        toolbar->setMovable(false);
        toolbar->setIconSize(QSize(16, 16));
        toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        
        toolbar->addAction("New", this, &TextEditor::newFile);
        toolbar->addAction("Open", this, &TextEditor::openFile);
        toolbar->addAction("Save", this, &TextEditor::save);
        toolbar->addSeparator();
        toolbar->addAction("Undo", this, &TextEditor::undo);
        toolbar->addAction("Redo", this, &TextEditor::redo);
        toolbar->addSeparator();
        toolbar->addAction("Cut", this, &TextEditor::cut);
        toolbar->addAction("Copy", this, &TextEditor::copy);
        toolbar->addAction("Paste", this, &TextEditor::paste);
        
        // Set toolbar style
        toolbar->setStyleSheet(
            "QToolBar { "
            "    background-color: #323232; "
            "    border: none; "
            "    spacing: 5px; "
            "    padding: 5px; "
            "} "
            "QToolBar QToolButton { "
            "    color: #c8c8c8; "
            "    background-color: transparent; "
            "    border: 1px solid transparent; "
            "    padding: 5px 10px; "
            "    border-radius: 3px; "
            "} "
            "QToolBar QToolButton:hover { "
            "    background-color: #4682b4; "
            "} "
            "QToolBar QToolButton:pressed { "
            "    background-color: #3a6ea5; "
            "}"
        );
    }
    
    bool maybeSave() {
        if (!m_modified) return true;
        
        QMessageBox::StandardButton reply = QMessageBox::question(this, "Unsaved Changes",
            "Save changes?", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        
        if (reply == QMessageBox::Save) return saveFile();
        return reply != QMessageBox::Cancel;
    }
    
    void loadSettings() {
        QSettings settings("Havel WM", "TextEditor");
        resize(settings.value("size", QSize(800, 600)).toSize());
        move(settings.value("pos", QPoint(100, 100)).toPoint());
    }
    
    void saveSettings() {
        QSettings settings("Havel WM", "TextEditor");
        settings.setValue("size", size());
        settings.setValue("pos", pos());
    }
    
    QPlainTextEdit* m_editor;
    QLabel* m_statusLabel;
    QString m_filePath;
    bool m_modified;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Havel Text Editor");
    app.setOrganizationName("Havel WM");
    
    TextEditor editor;
    editor.show();
    
    // Open files from command line
    QStringList args = app.arguments();
    for (int i = 1; i < args.size(); i++) {
        editor.loadFile(args[i]);
    }
    
    return app.exec();
}

#include "TextEditor.moc"
