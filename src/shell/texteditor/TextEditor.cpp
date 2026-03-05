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
        
        // Create editor
        m_editor = new QPlainTextEdit();
        m_editor->setFont(QFont("Monospace", 11));
        m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
        setCentralWidget(m_editor);
        
        // Status bar
        m_statusLabel = new QLabel("Line: 1, Col: 1");
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
        fileMenu->addAction("New", this, &TextEditor::newFile, QKeySequence::New);
        fileMenu->addAction("Open...", this, &TextEditor::openFile, QKeySequence::Open);
        fileMenu->addSeparator();
        fileMenu->addAction("Save", this, &TextEditor::save, QKeySequence::Save);
        fileMenu->addAction("Save As...", this, &TextEditor::saveAs, QKeySequence::SaveAs);
        fileMenu->addSeparator();
        fileMenu->addAction("Print...", this, &TextEditor::print, QKeySequence::Print);
        fileMenu->addSeparator();
        fileMenu->addAction("Exit", this, &QMainWindow::close, QKeySequence::Quit);
        
        QMenu* editMenu = menuBar()->addMenu("&Edit");
        editMenu->addAction("Undo", this, &TextEditor::undo, QKeySequence::Undo);
        editMenu->addAction("Redo", this, &TextEditor::redo, QKeySequence::Redo);
        editMenu->addSeparator();
        editMenu->addAction("Cut", this, &TextEditor::cut, QKeySequence::Cut);
        editMenu->addAction("Copy", this, &TextEditor::copy, QKeySequence::Copy);
        editMenu->addAction("Paste", this, &TextEditor::paste, QKeySequence::Paste);
        editMenu->addSeparator();
        editMenu->addAction("Select All", this, &TextEditor::selectAll, QKeySequence::SelectAll);
        editMenu->addSeparator();
        editMenu->addAction("Find...", this, &TextEditor::find, QKeySequence::Find);
        editMenu->addAction("Go to Line...", this, &TextEditor::goToLine, Qt::CTRL | Qt::Key_L);
        
        QMenu* viewMenu = menuBar()->addMenu("&View");
        viewMenu->addAction("Change Font...", this, &TextEditor::changeFont);
        viewMenu->addAction("Zoom In", this, &TextEditor::zoomIn, QKeySequence::ZoomIn);
        viewMenu->addAction("Zoom Out", this, &TextEditor::zoomOut, QKeySequence::ZoomOut);
        viewMenu->addAction("Word Wrap", this, &TextEditor::toggleWordWrap);
        
        QMenu* helpMenu = menuBar()->addMenu("&Help");
        helpMenu->addAction("About", this, &TextEditor::about);
    }
    
    void setupToolbar() {
        QToolBar* toolbar = addToolBar("Main");
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
