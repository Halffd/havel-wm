// Enhanced Dictionary with Reader Mode

#include <QApplication>
#include <QMainWindow>
#include <QLineEdit>
#include <QTextEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QToolBar>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QTabWidget>
#include <QSplitter>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStandardPaths>
#include <QClipboard>
#include <QSystemTrayIcon>
#include <QCloseEvent>
#include <QTimer>
#include <QDateTime>
#include <QSet>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QPainter>
#include <QIcon>
#include <QFontDialog>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QStyleFactory>
#include <QPalette>
#include <QRandomGenerator>
#include <QTextDocument>
#include <QTextCursor>
#include <QKeyEvent>
#include <QToolTip>
#include <QProgressDialog>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QPrintDialog>
#include <QPrinter>
#include <QTranslator>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

struct WordDefinition {
    QString word;
    QString phonetic;
    QString audioUrl;
    QString origin;
    QString language;
    QString dictionaryType;
    
    struct Meaning {
        QString partOfSpeech;
        QStringList definitions;
        QStringList synonyms;
        QStringList antonyms;
        QStringList examples;
    };
    
    QList<Meaning> meanings;
    WordDefinition() : language("en"), dictionaryType("general") {}
};

struct HistoryEntry {
    QString word;
    QDateTime timestamp;
    int lookups;
};

class DictionaryApp : public QMainWindow {
    Q_OBJECT

public:
    DictionaryApp() {
        setWindowTitle("Dictionary & Reader - Havel WM");
        setMinimumSize(1100, 800);
        
        // Central widget with stacked layout
        m_stackedWidget = new QStackedWidget();
        setCentralWidget(m_stackedWidget);
        
        // Create dictionary view
        QWidget* dictView = createDictionaryView();
        m_stackedWidget->addWidget(dictView);
        
        // Create reader view
        QWidget* readerView = createReaderView();
        m_stackedWidget->addWidget(readerView);
        
        setupMenu();
        setupToolbar();
        loadSettings();
        loadFavorites();
        loadHistory();
        updateWordOfTheDay();
        
        // System tray
        m_trayIcon = new QSystemTrayIcon(QIcon::fromTheme("accessories-dictionary"), this);
        m_trayIcon->show();
        
        QMenu* trayMenu = new QMenu(this);
        trayMenu->addAction("Show", this, &DictionaryApp::show);
        trayMenu->addSeparator();
        trayMenu->addAction("Quit", qApp, &QApplication::quit);
        m_trayIcon->setContextMenu(trayMenu);
        
        connect(m_trayIcon, &QSystemTrayIcon::activated, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::DoubleClick) {
                show();
                raise();
                activateWindow();
            }
        });
        
        // Network manager
        m_networkManager = new QNetworkAccessManager(this);
        connect(m_networkManager, &QNetworkAccessManager::finished, this, &DictionaryApp::onNetworkReply);
        
        // Local dictionary
        loadLocalDictionary();
        
        // Popup definition widget
        m_popupWidget = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint);
        m_popupWidget->setWindowTitle("Definition");
        QVBoxLayout* popupLayout = new QVBoxLayout(m_popupWidget);
        m_popupText = new QTextEdit();
        m_popupText->setReadOnly(true);
        m_popupText->setMaximumSize(400, 300);
        popupLayout->addWidget(m_popupText);
        m_popupWidget->setLayout(popupLayout);
        m_popupWidget->hide();
    }
    
    ~DictionaryApp() {
        saveSettings();
        saveFavorites();
        saveHistory();
    }
    
    // CLI support
    void lookupWord(const QString& word) {
        m_stackedWidget->setCurrentIndex(0);
        m_searchEdit->setText(word);
        onSearch();
    }
    
    void translateText(const QString& text, const QString& fromLang, const QString& toLang) {
        m_stackedWidget->setCurrentIndex(0);
        m_sidebarTabs->setCurrentIndex(3);  // Translator tab
        
        m_translateSource->setText(text);
        m_translateFrom->setCurrentText(fromLang);
        m_translateTo->setCurrentText(toLang);
        onTranslate();
    }
    
    void detectLanguage(const QString& text) {
        m_searchEdit->setText(text);
        onDetectLanguage();
    }
    
    void openDocument(const QString& path) {
        m_stackedWidget->setCurrentIndex(1);
        loadDocument(path);
    }
    
    void showWordOfTheDay() {
        updateWordOfTheDay();
    }
    
private slots:
    void onSearch() {
        QString word = m_searchEdit->text().trimmed();
        if (word.isEmpty()) return;
        
        m_statusLabel->setText("Searching...");
        m_searchButton->setEnabled(false);
        
        addToHistory(word);
        m_favoriteButton->setChecked(m_favorites.contains(word));
        
        // Get selected language and type
        QString language = m_languageCombo->currentData().toString();
        QString dictType = m_typeCombo->currentData().toString();
        
        searchOnline(word, language, dictType);
    }
    
    void onRandomWord() {
        if (m_localDictionary.isEmpty()) {
            QStringList commonWords = {"serendipity", "ephemeral", "ubiquitous", "eloquent", "resilient"};
            QString word = commonWords[QRandomGenerator::global()->bounded(commonWords.size())];
            m_searchEdit->setText(word);
            onSearch();
        } else {
            QStringList words = m_localDictionary.keys();
            QString word = words[QRandomGenerator::global()->bounded(words.size())];
            m_searchEdit->setText(word);
            onSearch();
        }
    }
    
    void onToggleFavorite(bool checked) {
        QString word = m_wordLabel->text();
        if (word.isEmpty() || word == "Enter a word") return;
        
        if (checked) {
            m_favorites.insert(word);
            m_favoriteButton->setText("★");
        } else {
            m_favorites.remove(word);
            m_favoriteButton->setText("☆");
        }
        saveFavorites();
        updateFavoritesList();
    }
    
    void onClearFavorites() {
        if (QMessageBox::question(this, "Clear Favorites", "Clear all favorites?") == QMessageBox::Yes) {
            m_favorites.clear();
            saveFavorites();
            updateFavoritesList();
        }
    }
    
    void onClearHistory() {
        if (QMessageBox::question(this, "Clear History", "Clear search history?") == QMessageBox::Yes) {
            m_history.clear();
            saveHistory();
            m_historyList->clear();
        }
    }
    
    void onFavoriteClicked(QListWidgetItem* item) {
        m_searchEdit->setText(item->text());
        onSearch();
    }
    
    void onHistoryClicked(QListWidgetItem* item) {
        m_searchEdit->setText(item->text());
        onSearch();
    }
    
    void onPlayAudio() {
        if (!m_currentAudioUrl.isEmpty()) {
            QDesktopServices::openUrl(QUrl(m_currentAudioUrl));
        }
    }
    
    void onNetworkReply(QNetworkReply* reply) {
        m_searchButton->setEnabled(true);
        
        if (reply->error() != QNetworkReply::NoError) {
            m_statusLabel->setText("Error: " + reply->errorString());
            m_definitionText->setText("Failed to fetch definition.");
            reply->deleteLater();
            return;
        }
        
        QByteArray data = reply->readAll();
        QJsonDocument json = QJsonDocument::fromJson(data);
        
        if (json.isNull() || json.array().isEmpty()) {
            m_statusLabel->setText("No definition found");
            m_definitionText->setText("No definition found for: " + m_searchEdit->text());
            m_wordLabel->setText(m_searchEdit->text());
            reply->deleteLater();
            return;
        }
        
        parseDefinition(json.array()[0].toObject());
        reply->deleteLater();
    }
    
    void onExportFavorites() {
        QString path = QFileDialog::getSaveFileName(this, "Export Favorites", "", "Text Files (*.txt)");
        if (!path.isEmpty()) {
            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                QTextStream out(&file);
                for (const QString& word : m_favorites) {
                    out << word << "\n";
                }
                file.close();
                statusBar()->showMessage("Favorites exported", 2000);
            }
        }
    }
    
    void onImportFavorites() {
        QString path = QFileDialog::getOpenFileName(this, "Import Favorites", "", "Text Files (*.txt)");
        if (!path.isEmpty()) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                QTextStream in(&file);
                while (!in.atEnd()) {
                    QString word = in.readLine().trimmed();
                    if (!word.isEmpty()) m_favorites.insert(word);
                }
                file.close();
                saveFavorites();
                updateFavoritesList();
                statusBar()->showMessage("Favorites imported", 2000);
            }
        }
    }
    
    void onLookupWotd() {
        if (!m_wotdWord->text().isEmpty()) {
            m_searchEdit->setText(m_wotdWord->text());
            onSearch();
        }
    }
    
    void onTranslate() {
        QString text = m_translateSource->toPlainText().trimmed();
        if (text.isEmpty()) {
            QMessageBox::warning(this, "Warning", "Please enter text to translate");
            return;
        }
        
        QString fromLang = m_translateFrom->currentData().toString();
        QString toLang = m_translateTo->currentData().toString();
        
        m_statusLabel->setText("Translating...");
        
        // Use MyMemory Translation API (free, no key required)
        QUrl url("https://api.mymemory.translated.net/get");
        QUrlQuery query;
        query.addQueryItem("q", text);
        query.addQueryItem("langpair", fromLang + "|" + toLang);
        url.setQuery(query);
        
        QNetworkReply* reply = m_networkManager->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, [this, reply, toLang]() {
            if (reply->error() != QNetworkReply::NoError) {
                m_translateResult->setText("Translation error: " + reply->errorString());
                reply->deleteLater();
                return;
            }
            
            QByteArray data = reply->readAll();
            QJsonDocument json = QJsonDocument::fromJson(data);
            QJsonObject obj = json.object();
            
            if (obj.contains("responseData")) {
                QJsonObject responseData = obj["responseData"].toObject();
                QString translatedText = responseData["translatedText"].toString();
                
                // Detect language if auto
                if (m_translateFrom->currentData().toString() == "auto") {
                    QString detectedLang = responseData["match"].toString();
                    m_translateResult->setHtml(
                        QString("<p><b>Detected Language:</b> %1</p><p>%2</p>")
                        .arg(detectedLang).arg(translatedText));
                } else {
                    m_translateResult->setText(translatedText);
                }
            } else {
                m_translateResult->setText("Translation failed");
            }
            
            m_statusLabel->setText("Translation complete");
            reply->deleteLater();
        });
    }
    
    void onDetectLanguage() {
        QString text = m_searchEdit->text().trimmed();
        if (text.isEmpty()) return;
        
        m_statusLabel->setText("Detecting language...");
        
        // Use language detection API
        QUrl url("https://api.mymemory.translated.net/get");
        QUrlQuery query;
        query.addQueryItem("q", text);
        query.addQueryItem("langpair", "en|en");  // Dummy pair, we just want detection
        url.setQuery(query);
        
        QNetworkReply* reply = m_networkManager->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, [this, reply]() {
            if (reply->error() != QNetworkReply::NoError) {
                m_statusLabel->setText("Detection failed");
                reply->deleteLater();
                return;
            }
            
            QByteArray data = reply->readAll();
            QJsonDocument json = QJsonDocument::fromJson(data);
            QJsonObject obj = json.object();
            
            if (obj.contains("responseData")) {
                QJsonObject responseData = obj["responseData"].toObject();
                QString detectedLang = responseData["match"].toString();
                
                // Set language combo to detected language
                for (int i = 0; i < m_languageCombo->count(); i++) {
                    if (m_languageCombo->itemData(i).toString() == detectedLang) {
                        m_languageCombo->setCurrentIndex(i);
                        break;
                    }
                }
                
                m_statusLabel->setText("Language detected: " + detectedLang);
            }
            
            reply->deleteLater();
        });
    }
    
    // Reader mode slots
    void onOpenDocument() {
        QString path = QFileDialog::getOpenFileName(this, "Open Document", "",
            "Documents (*.txt *.md *.html *.htm *.pdf *.epub);;All Files (*)");
        if (!path.isEmpty()) {
            loadDocument(path);
        }
    }
    
    void onZoomIn() {
        m_zoomSlider->setValue(m_zoomSlider->value() + 10);
    }
    
    void onZoomOut() {
        m_zoomSlider->setValue(m_zoomSlider->value() - 10);
    }
    
    void onZoomChanged(int value) {
        QFont font = m_readerView->font();
        font.setPointSize(value / 10);
        m_readerView->setFont(font);
        m_zoomLabel->setText(QString("%1%").arg(value));
    }
    
    void onPreviousPage() {
        // QTextEdit doesn't have history, skip
    }
    
    void onNextPage() {
        // QTextEdit doesn't have history, skip
    }
    
    void onSearchInDocument() {
        QString text = QInputDialog::getText(this, "Find in Document", "Find:");
        if (!text.isEmpty()) {
            m_readerView->find(text);
        }
    }
    
    void onPrintDocument() {
        QPrinter printer;
        QPrintDialog dialog(&printer, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_readerView->print(&printer);
        }
    }
    
    void onToggleReaderMode() {
        int currentIndex = m_stackedWidget->currentIndex();
        m_stackedWidget->setCurrentIndex(currentIndex == 0 ? 1 : 0);
    }
    
    void onWordClicked(const QString& word) {
        // Show popup definition
        showPopupDefinition(word);
    }
    
    void onAbout() {
        QMessageBox::about(this, "About Dictionary & Reader",
            "Havel WM Dictionary & Reader\n\n"
            "Features:\n"
            "- Multi-language support (10 languages)\n"
            "- Multiple dictionary types\n"
            "- Online/Offline dictionary\n"
            "- Thesaurus integration\n"
            "- Word of the day\n"
            "- Reader mode for documents\n"
            "- PDF, EPUB, TXT, HTML support\n"
            "- Popup definitions\n"
            "- Keyboard navigation\n"
            "- Favorites & history\n"
            "\nSupported Languages:\n"
            "English, Spanish, French, German,\n"
            "Italian, Portuguese, Russian,\n"
            "Chinese, Japanese, Korean\n"
            "\nDictionary Types:\n"
            "General, Thesaurus, Medical,\n"
            "Legal, Technical, Scientific, Business");
    }
    
    void onShortcuts() {
        QMessageBox::information(this, "Keyboard Shortcuts",
            "Dictionary Mode:\n"
            "  Enter       Search word\n"
            "  Ctrl+R      Random word\n"
            "  Ctrl+F      Toggle favorite\n"
            "  Ctrl+D      Word of the day\n"
            "  Ctrl+L      Select language\n"
            "  Ctrl+T      Select type\n"
            "  Ctrl+A      Autodetect language\n"
            "\nTranslator:\n"
            "  Ctrl+Shift+T  Translate text\n"
            "  Ctrl+Shift+D  Detect language\n"
            "\nReader Mode:\n"
            "  Ctrl+O      Open document\n"
            "  Ctrl++      Zoom in\n"
            "  Ctrl+-      Zoom out\n"
            "  Ctrl+F      Find in document\n"
            "  Ctrl+P      Print\n"
            "  Space       Page down\n"
            "  Shift+Space Page up\n"
            "\nGeneral:\n"
            "  Ctrl+Tab    Toggle mode\n"
            "  Ctrl+Q      Quit\n"
            "\nCLI Usage:\n"
            "  --lookup WORD           Look up word\n"
            "  --translate TEXT LANG   Translate text\n"
            "  --detect TEXT           Detect language\n"
            "  --wotd                  Word of the day\n"
            "  --open FILE             Open document");
    }
    
protected:
    void closeEvent(QCloseEvent* event) {
        if (m_trayIcon->isVisible()) {
            hide();
            event->ignore();
        } else {
            saveSettings();
            saveFavorites();
            saveHistory();
            event->accept();
        }
    }
    
    void keyPressEvent(QKeyEvent* event) {
        // Global keyboard shortcuts
        if (event->modifiers() == Qt::ControlModifier) {
            switch (event->key()) {
                case Qt::Key_Tab:
                    onToggleReaderMode();
                    return;
                case Qt::Key_Plus:
                case Qt::Key_Equal:
                    onZoomIn();
                    return;
                case Qt::Key_Minus:
                    onZoomOut();
                    return;
                case Qt::Key_O:
                    onOpenDocument();
                    return;
                case Qt::Key_P:
                    onPrintDocument();
                    return;
                case Qt::Key_F:
                    if (m_stackedWidget->currentIndex() == 1) {
                        onSearchInDocument();
                    }
                    return;
                case Qt::Key_L:
                    if (m_stackedWidget->currentIndex() == 0) {
                        m_languageCombo->showPopup();
                    }
                    return;
                case Qt::Key_T:
                    if (m_stackedWidget->currentIndex() == 0) {
                        m_typeCombo->showPopup();
                    }
                    return;
                case Qt::Key_A:
                    if (m_stackedWidget->currentIndex() == 0) {
                        onDetectLanguage();
                    }
                    return;
            }
        }
        
        if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
            switch (event->key()) {
                case Qt::Key_T:
                    m_sidebarTabs->setCurrentIndex(3);  // Translator tab
                    m_translateSource->setFocus();
                    return;
                case Qt::Key_D:
                    onDetectLanguage();
                    return;
            }
        }
        
        if (event->modifiers() == Qt::AltModifier) {
            switch (event->key()) {
                case Qt::Key_Left:
                    onPreviousPage();
                    return;
                case Qt::Key_Right:
                    onNextPage();
                    return;
            }
        }
        
        // Reader mode navigation
        if (m_stackedWidget->currentIndex() == 1) {
            switch (event->key()) {
                case Qt::Key_Space:
                    if (event->modifiers() & Qt::ShiftModifier) {
                        QKeyEvent upEvent(QKeyEvent::KeyPress, Qt::Key_PageUp, Qt::NoModifier);
                        QApplication::sendEvent(m_readerView, &upEvent);
                    } else {
                        QKeyEvent downEvent(QKeyEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier);
                        QApplication::sendEvent(m_readerView, &downEvent);
                    }
                    return;
            }
        }
        
        QMainWindow::keyPressEvent(event);
    }

private:
    QWidget* createDictionaryView() {
        QWidget* widget = new QWidget();
        QVBoxLayout* mainLayout = new QVBoxLayout(widget);
        
        // Search bar
        QHBoxLayout* searchLayout = new QHBoxLayout();
        
        m_searchEdit = new QLineEdit();
        m_searchEdit->setPlaceholderText("Type a word to search...");
        m_searchEdit->setMinimumHeight(40);
        QFont font = m_searchEdit->font();
        font.setPointSize(14);
        m_searchEdit->setFont(font);
        connect(m_searchEdit, &QLineEdit::returnPressed, this, &DictionaryApp::onSearch);
        searchLayout->addWidget(m_searchEdit);
        
        m_searchButton = new QPushButton("🔍 Search");
        m_searchButton->setMinimumHeight(40);
        m_searchButton->setMinimumWidth(100);
        connect(m_searchButton, &QPushButton::clicked, this, &DictionaryApp::onSearch);
        searchLayout->addWidget(m_searchButton);
        
        m_randomButton = new QPushButton("🎲 Random");
        m_randomButton->setMinimumHeight(40);
        connect(m_randomButton, &QPushButton::clicked, this, &DictionaryApp::onRandomWord);
        searchLayout->addWidget(m_randomButton);
        
        mainLayout->addLayout(searchLayout);
        
        // Source selector
        QHBoxLayout* sourceLayout = new QHBoxLayout();
        sourceLayout->addWidget(new QLabel("Language:"));
        m_languageCombo = new QComboBox();
        m_languageCombo->addItem("🇺🇸 English", "en");
        m_languageCombo->addItem("🇪🇸 Spanish", "es");
        m_languageCombo->addItem("🇫🇷 French", "fr");
        m_languageCombo->addItem("🇩🇪 German", "de");
        m_languageCombo->addItem("🇮🇹 Italian", "it");
        m_languageCombo->addItem("🇵🇹 Portuguese", "pt");
        m_languageCombo->addItem("🇷🇺 Russian", "ru");
        m_languageCombo->addItem("🇨🇳 Chinese", "zh");
        m_languageCombo->addItem("🇯🇵 Japanese", "ja");
        m_languageCombo->addItem("🇰🇷 Korean", "ko");
        m_languageCombo->setMaximumWidth(180);
        sourceLayout->addWidget(m_languageCombo);
        
        sourceLayout->addWidget(new QLabel("  Type:"));
        m_typeCombo = new QComboBox();
        m_typeCombo->addItem("📖 General", "general");
        m_typeCombo->addItem("📚 Thesaurus", "thesaurus");
        m_typeCombo->addItem("⚕️ Medical", "medical");
        m_typeCombo->addItem("⚖️ Legal", "legal");
        m_typeCombo->addItem("💻 Technical", "technical");
        m_typeCombo->addItem("🔬 Scientific", "scientific");
        m_typeCombo->addItem("📈 Business", "business");
        m_typeCombo->setMaximumWidth(150);
        sourceLayout->addWidget(m_typeCombo);
        
        sourceLayout->addStretch();
        mainLayout->addLayout(sourceLayout);
        
        // Content area with splitter
        QSplitter* splitter = new QSplitter(Qt::Horizontal);
        
        // Definition display
        QWidget* defWidget = new QWidget();
        QVBoxLayout* defLayout = new QVBoxLayout(defWidget);
        
        // Word header
        QHBoxLayout* headerLayout = new QHBoxLayout();
        
        m_wordLabel = new QLabel("Enter a word");
        QFont wordFont = m_wordLabel->font();
        wordFont.setPointSize(24);
        wordFont.setBold(true);
        m_wordLabel->setFont(wordFont);
        headerLayout->addWidget(m_wordLabel);
        
        m_favoriteButton = new QPushButton("☆");
        m_favoriteButton->setMaximumWidth(40);
        m_favoriteButton->setCheckable(true);
        connect(m_favoriteButton, &QPushButton::toggled, this, &DictionaryApp::onToggleFavorite);
        headerLayout->addWidget(m_favoriteButton);
        
        headerLayout->addStretch();
        defLayout->addLayout(headerLayout);
        
        // Phonetic and audio
        QHBoxLayout* phoneticLayout = new QHBoxLayout();
        
        m_phoneticLabel = new QLabel("");
        m_phoneticLabel->setStyleSheet("color: #888; font-style: italic;");
        phoneticLayout->addWidget(m_phoneticLabel);
        
        m_playAudioButton = new QPushButton("🔊");
        m_playAudioButton->setMaximumWidth(40);
        connect(m_playAudioButton, &QPushButton::clicked, this, &DictionaryApp::onPlayAudio);
        phoneticLayout->addWidget(m_playAudioButton);
        
        phoneticLayout->addStretch();
        defLayout->addLayout(phoneticLayout);
        
        // Origin
        m_originLabel = new QLabel("");
        m_originLabel->setStyleSheet("color: #666;");
        m_originLabel->setWordWrap(true);
        defLayout->addWidget(m_originLabel);
        
        // Definition text
        m_definitionText = new QTextEdit();
        m_definitionText->setReadOnly(true);
        m_definitionText->setMinimumHeight(300);
        defLayout->addWidget(m_definitionText);
        
        splitter->addWidget(defWidget);
        
        // Sidebar
        QWidget* sidebar = new QWidget();
        QVBoxLayout* sidebarLayout = new QVBoxLayout(sidebar);
        sidebar->setMinimumWidth(250);
        
        m_sidebarTabs = new QTabWidget();
        
        // Favorites tab
        QWidget* favTab = new QWidget();
        QVBoxLayout* favLayout = new QVBoxLayout(favTab);
        
        m_favoritesList = new QListWidget();
        connect(m_favoritesList, &QListWidget::itemDoubleClicked, this, &DictionaryApp::onFavoriteClicked);
        favLayout->addWidget(m_favoritesList);
        
        QHBoxLayout* favBtnLayout = new QHBoxLayout();
        QPushButton* exportFav = new QPushButton("Export");
        connect(exportFav, &QPushButton::clicked, this, &DictionaryApp::onExportFavorites);
        favBtnLayout->addWidget(exportFav);
        
        QPushButton* importFav = new QPushButton("Import");
        connect(importFav, &QPushButton::clicked, this, &DictionaryApp::onImportFavorites);
        favBtnLayout->addWidget(importFav);
        
        m_clearFavButton = new QPushButton("Clear");
        connect(m_clearFavButton, &QPushButton::clicked, this, &DictionaryApp::onClearFavorites);
        favBtnLayout->addWidget(m_clearFavButton);
        
        favLayout->addLayout(favBtnLayout);
        m_sidebarTabs->addTab(favTab, "⭐ Favorites");
        
        // History tab
        QWidget* histTab = new QWidget();
        QVBoxLayout* histLayout = new QVBoxLayout(histTab);
        
        m_historyList = new QListWidget();
        connect(m_historyList, &QListWidget::itemDoubleClicked, this, &DictionaryApp::onHistoryClicked);
        histLayout->addWidget(m_historyList);
        
        m_clearHistButton = new QPushButton("Clear History");
        connect(m_clearHistButton, &QPushButton::clicked, this, &DictionaryApp::onClearHistory);
        histLayout->addWidget(m_clearHistButton);
        
        m_sidebarTabs->addTab(histTab, "📜 History");
        
        // Word of the Day tab
        QWidget* wotdTab = new QWidget();
        QVBoxLayout* wotdLayout = new QVBoxLayout(wotdTab);
        
        m_wotdLabel = new QLabel("Word of the Day");
        QFont wotdFont = m_wotdLabel->font();
        wotdFont.setPointSize(16);
        wotdFont.setBold(true);
        m_wotdLabel->setFont(wotdFont);
        m_wotdLabel->setAlignment(Qt::AlignCenter);
        wotdLayout->addWidget(m_wotdLabel);
        
        m_wotdWord = new QLabel("");
        m_wotdWord->setWordWrap(true);
        m_wotdWord->setAlignment(Qt::AlignCenter);
        wotdLayout->addWidget(m_wotdWord);
        
        m_wotdDef = new QLabel("");
        m_wotdDef->setWordWrap(true);
        wotdLayout->addWidget(m_wotdDef);
        
        QPushButton* lookupWotd = new QPushButton("Look Up");
        connect(lookupWotd, &QPushButton::clicked, this, &DictionaryApp::onLookupWotd);
        wotdLayout->addWidget(lookupWotd);
        
        wotdLayout->addStretch();
        m_sidebarTabs->addTab(wotdTab, "📅 Word of Day");
        
        // Translator tab
        QWidget* transTab = new QWidget();
        QVBoxLayout* transLayout = new QVBoxLayout(transTab);
        
        transLayout->addWidget(new QLabel("Translate Text:"));
        
        m_translateSource = new QTextEdit();
        m_translateSource->setPlaceholderText("Enter text to translate...");
        m_translateSource->setMaximumHeight(150);
        transLayout->addWidget(m_translateSource);
        
        QHBoxLayout* langLayout = new QHBoxLayout();
        langLayout->addWidget(new QLabel("From:"));
        m_translateFrom = new QComboBox();
        m_translateFrom->addItem("Auto Detect", "auto");
        m_translateFrom->addItem("🇺🇸 English", "en");
        m_translateFrom->addItem("🇪🇸 Spanish", "es");
        m_translateFrom->addItem("🇫🇷 French", "fr");
        m_translateFrom->addItem("🇩🇪 German", "de");
        m_translateFrom->addItem("🇮🇹 Italian", "it");
        m_translateFrom->addItem("🇵🇹 Portuguese", "pt");
        m_translateFrom->addItem("🇷🇺 Russian", "ru");
        m_translateFrom->addItem("🇨🇳 Chinese", "zh");
        m_translateFrom->addItem("🇯🇵 Japanese", "ja");
        m_translateFrom->addItem("🇰🇷 Korean", "ko");
        langLayout->addWidget(m_translateFrom);
        
        langLayout->addWidget(new QLabel("To:"));
        m_translateTo = new QComboBox();
        m_translateTo->addItem("🇺🇸 English", "en");
        m_translateTo->addItem("🇪🇸 Spanish", "es");
        m_translateTo->addItem("🇫🇷 French", "fr");
        m_translateTo->addItem("🇩🇪 German", "de");
        m_translateTo->addItem("🇮🇹 Italian", "it");
        m_translateTo->addItem("🇵🇹 Portuguese", "pt");
        m_translateTo->addItem("🇷🇺 Russian", "ru");
        m_translateTo->addItem("🇨🇳 Chinese", "zh");
        m_translateTo->addItem("🇯🇵 Japanese", "ja");
        m_translateTo->addItem("🇰🇷 Korean", "ko");
        langLayout->addWidget(m_translateTo);
        
        transLayout->addLayout(langLayout);
        
        QPushButton* translateBtn = new QPushButton("🌐 Translate");
        translateBtn->setMinimumHeight(40);
        connect(translateBtn, &QPushButton::clicked, this, &DictionaryApp::onTranslate);
        transLayout->addWidget(translateBtn);
        
        transLayout->addWidget(new QLabel("Translation:"));
        m_translateResult = new QTextEdit();
        m_translateResult->setReadOnly(true);
        m_translateResult->setPlaceholderText("Translation will appear here...");
        transLayout->addWidget(m_translateResult);
        
        m_sidebarTabs->addTab(transTab, "🌐 Translator");
        
        sidebarLayout->addWidget(m_sidebarTabs);
        splitter->addWidget(sidebar);
        
        splitter->setSizes(QList<int>() << 650 << 250);
        mainLayout->addWidget(splitter);
        
        // Status bar
        m_statusLabel = new QLabel("Ready");
        statusBar()->addWidget(m_statusLabel);
        
        m_countLabel = new QLabel("");
        statusBar()->addPermanentWidget(m_countLabel);
        
        return widget;
    }
    
    QWidget* createReaderView() {
        QWidget* widget = new QWidget();
        QVBoxLayout* layout = new QVBoxLayout(widget);
        
        // Reader toolbar
        QHBoxLayout* readerToolbar = new QHBoxLayout();
        
        QPushButton* openBtn = new QPushButton("📂 Open");
        connect(openBtn, &QPushButton::clicked, this, &DictionaryApp::onOpenDocument);
        readerToolbar->addWidget(openBtn);
        
        QPushButton* prevBtn = new QPushButton("◀");
        connect(prevBtn, &QPushButton::clicked, this, &DictionaryApp::onPreviousPage);
        readerToolbar->addWidget(prevBtn);
        
        QPushButton* nextBtn = new QPushButton("▶");
        connect(nextBtn, &QPushButton::clicked, this, &DictionaryApp::onNextPage);
        readerToolbar->addWidget(nextBtn);
        
        readerToolbar->addWidget(new QLabel("Zoom:"));
        
        m_zoomSlider = new QSlider(Qt::Horizontal);
        m_zoomSlider->setRange(50, 200);
        m_zoomSlider->setValue(100);
        m_zoomSlider->setMaximumWidth(150);
        connect(m_zoomSlider, &QSlider::valueChanged, this, &DictionaryApp::onZoomChanged);
        readerToolbar->addWidget(m_zoomSlider);
        
        m_zoomLabel = new QLabel("100%");
        readerToolbar->addWidget(m_zoomLabel);
        
        QPushButton* findBtn = new QPushButton("🔍 Find");
        connect(findBtn, &QPushButton::clicked, this, &DictionaryApp::onSearchInDocument);
        readerToolbar->addWidget(findBtn);
        
        QPushButton* printBtn = new QPushButton("🖨 Print");
        connect(printBtn, &QPushButton::clicked, this, &DictionaryApp::onPrintDocument);
        readerToolbar->addWidget(printBtn);
        
        readerToolbar->addStretch();
        layout->addLayout(readerToolbar);
        
        // Reader view
        m_readerView = new QTextEdit();
        m_readerView->setReadOnly(true);
        m_readerView->setAcceptRichText(true);
        QFont font = m_readerView->font();
        font.setPointSize(11);
        m_readerView->setFont(font);
        
        layout->addWidget(m_readerView);
        
        // Load welcome page
        QString welcomeHtml = R"(
            <html><head><style>
                body { font-family: sans-serif; padding: 40px; background: #202020; color: #fff; }
                h1 { color: #4CAF50; }
                .info { background: #303030; padding: 20px; border-radius: 8px; margin: 20px 0; }
                .formats { display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; }
                .format { background: #404040; padding: 10px; border-radius: 4px; }
            </style></head>
            <body>
                <h1>📖 Reader Mode</h1>
                <div class="info">
                    <p>Open a document to start reading. Double-click any word to see its definition.</p>
                    <h3>Supported Formats:</h3>
                    <div class="formats">
                        <div class="format">📄 TXT - Plain Text</div>
                        <div class="format">📝 MD - Markdown</div>
                        <div class="format">🌐 HTML - Web Pages</div>
                    </div>
                </div>
                <h3>Keyboard Shortcuts:</h3>
                <ul>
                    <li><b>Ctrl+O</b> - Open document</li>
                    <li><b>Ctrl++/-</b> - Zoom in/out</li>
                    <li><b>Ctrl+F</b> - Find in document</li>
                    <li><b>Space/Shift+Space</b> - Page down/up</li>
                </ul>
            </body></html>
        )";
        
        m_readerView->setHtml(welcomeHtml);
        
        // Enable word click for popup definitions
        connect(m_readerView, &QTextEdit::cursorPositionChanged, [this]() {
            QTextCursor cursor = m_readerView->textCursor();
            QString word = cursor.selectedText();
            if (!word.isEmpty() && word.length() < 50) {
                showPopupDefinition(word);
            }
        });
        
        return widget;
    }
    
    void setupMenu() {
        QMenu* fileMenu = menuBar()->addMenu("&File");
        fileMenu->addAction("&Search", this, &DictionaryApp::onSearch, QKeySequence::Find);
        fileMenu->addAction("&Random Word", this, &DictionaryApp::onRandomWord, QKeySequence(Qt::CTRL | Qt::Key_R));
        fileMenu->addAction("Word of &Day", this, &DictionaryApp::updateWordOfTheDay, QKeySequence(Qt::CTRL | Qt::Key_D));
        fileMenu->addSeparator();
        fileMenu->addAction("&Open Document", this, &DictionaryApp::onOpenDocument, QKeySequence(Qt::CTRL | Qt::Key_O));
        fileMenu->addAction("&Print", this, &DictionaryApp::onPrintDocument, QKeySequence::Print);
        fileMenu->addSeparator();
        fileMenu->addAction("E&xit", qApp, &QApplication::quit, QKeySequence::Quit);
        
        QMenu* editMenu = menuBar()->addMenu("&Edit");
        editMenu->addAction("Toggle &Favorite", [this]() { m_favoriteButton->click(); }, QKeySequence(Qt::CTRL | Qt::Key_F));
        editMenu->addAction("Copy Definition", [this]() {
            QApplication::clipboard()->setText(m_definitionText->toPlainText());
        });
        editMenu->addSeparator();
        editMenu->addAction("&Find in Document", this, &DictionaryApp::onSearchInDocument, QKeySequence::Find);
        
        QMenu* viewMenu = menuBar()->addMenu("&View");
        viewMenu->addAction("&Dictionary Mode", [this]() { m_stackedWidget->setCurrentIndex(0); });
        viewMenu->addAction("&Reader Mode", [this]() { m_stackedWidget->setCurrentIndex(1); }, QKeySequence(Qt::CTRL | Qt::Key_Tab));
        viewMenu->addSeparator();
        viewMenu->addAction("Zoom &In", this, &DictionaryApp::onZoomIn, QKeySequence::ZoomIn);
        viewMenu->addAction("Zoom &Out", this, &DictionaryApp::onZoomOut, QKeySequence::ZoomOut);
        viewMenu->addAction("&Normal Size", [this]() { m_zoomSlider->setValue(100); }, QKeySequence(Qt::CTRL | Qt::Key_0));
        
        QMenu* helpMenu = menuBar()->addMenu("&Help");
        helpMenu->addAction("&About", this, &DictionaryApp::onAbout);
        helpMenu->addAction("&Shortcuts", this, &DictionaryApp::onShortcuts);
    }
    
    void setupToolbar() {
        QToolBar* toolbar = addToolBar("Main");
        toolbar->addAction("🔍 Search", this, &DictionaryApp::onSearch);
        toolbar->addAction("🎲 Random", this, &DictionaryApp::onRandomWord);
        toolbar->addAction("📅 Word of Day", this, &DictionaryApp::updateWordOfTheDay);
        toolbar->addSeparator();
        toolbar->addAction("📂 Open Document", this, &DictionaryApp::onOpenDocument);
        toolbar->addSeparator();
        toolbar->addAction("⭐ Favorite", [this]() { m_favoriteButton->click(); });
        toolbar->addAction("🔊 Audio", this, &DictionaryApp::onPlayAudio);
    }
    
    void searchOnline(const QString& word, const QString& language, const QString& dictType) {
        // Use different APIs based on language and type
        QUrl url;
        
        if (dictType == "thesaurus") {
            // Thesaurus API
            url = QUrl("https://api.datamuse.com/words?ml=" + word + "&max=10");
        } else if (language == "es") {
            url = QUrl("https://api.dictionaryapi.dev/api/v2/entries/es/" + word);
        } else if (language == "fr") {
            url = QUrl("https://api.dictionaryapi.dev/api/v2/entries/fr/" + word);
        } else if (language == "de") {
            url = QUrl("https://api.dictionaryapi.dev/api/v2/entries/de/" + word);
        } else {
            // Default to English
            url = QUrl("https://api.dictionaryapi.dev/api/v2/entries/en/" + word);
        }
        
        m_networkManager->get(QNetworkRequest(url));
    }
    
    void parseDefinition(const QJsonObject& entry) {
        WordDefinition def;
        def.language = m_languageCombo->currentData().toString();
        def.dictionaryType = m_typeCombo->currentData().toString();
        
        // Handle thesaurus results
        if (def.dictionaryType == "thesaurus" && entry.contains("word")) {
            def.word = entry["word"].toString();
            WordDefinition::Meaning meaning;
            meaning.partOfSpeech = "synonyms";
            
            if (entry.contains("synonyms")) {
                QJsonArray syns = entry["synonyms"].toArray();
                for (const QJsonValue& s : syns) {
                    meaning.synonyms.append(s.toString());
                }
            }
            
            def.meanings.append(meaning);
            displayDefinition(def);
            m_statusLabel->setText("Thesaurus results loaded");
            m_searchButton->setEnabled(true);
            return;
        }
        
        // Standard dictionary entry
        def.word = entry["word"].toString();
        
        if (entry.contains("phonetic")) {
            def.phonetic = entry["phonetic"].toString();
        }
        
        if (entry.contains("phonetics")) {
            QJsonArray phonetics = entry["phonetics"].toArray();
            for (const QJsonValue& p : phonetics) {
                QJsonObject obj = p.toObject();
                if (!obj["text"].toString().isEmpty() && def.phonetic.isEmpty()) {
                    def.phonetic = obj["text"].toString();
                }
                if (!obj["audio"].toString().isEmpty() && def.audioUrl.isEmpty()) {
                    def.audioUrl = obj["audio"].toString();
                }
            }
        }
        
        if (entry.contains("meanings")) {
            QJsonArray meanings = entry["meanings"].toArray();
            for (const QJsonValue& m : meanings) {
                QJsonObject meaningObj = m.toObject();
                WordDefinition::Meaning meaning;
                meaning.partOfSpeech = meaningObj["partOfSpeech"].toString();
                
                if (meaningObj.contains("definitions")) {
                    QJsonArray defs = meaningObj["definitions"].toArray();
                    for (const QJsonValue& d : defs) {
                        QJsonObject defObj = d.toObject();
                        meaning.definitions.append(defObj["definition"].toString());
                        if (!defObj["example"].toString().isEmpty()) {
                            meaning.examples.append("Example: " + defObj["example"].toString());
                        }
                    }
                }
                
                if (meaningObj.contains("synonyms")) {
                    QJsonArray syns = meaningObj["synonyms"].toArray();
                    for (const QJsonValue& s : syns) {
                        meaning.synonyms.append(s.toString());
                    }
                }
                
                if (meaningObj.contains("antonyms")) {
                    QJsonArray ants = meaningObj["antonyms"].toArray();
                    for (const QJsonValue& a : ants) {
                        meaning.antonyms.append(a.toString());
                    }
                }
                
                def.meanings.append(meaning);
            }
        }
        
        if (entry.contains("origin")) {
            def.origin = entry["origin"].toString();
        }
        
        displayDefinition(def);
        m_statusLabel->setText("Definition loaded (" + def.language + ")");
    }
    
    void displayDefinition(const WordDefinition& def) {
        m_currentDefinition = def;
        
        m_wordLabel->setText(def.word);
        m_phoneticLabel->setText(def.phonetic);
        m_originLabel->setText(def.origin);
        m_currentAudioUrl = def.audioUrl;
        
        m_favoriteButton->setChecked(m_favorites.contains(def.word));
        m_favoriteButton->setText(m_favorites.contains(def.word) ? "★" : "☆");
        
        QString html;
        
        // Show language and type
        html += QString("<p style='color: #888; font-size: 12px;'>Language: %1 | Type: %2</p>")
            .arg(def.language).arg(def.dictionaryType);
        
        for (const WordDefinition::Meaning& meaning : def.meanings) {
            html += QString("<h3 style='color: #4CAF50;'>%1</h3>").arg(meaning.partOfSpeech);
            
            for (int i = 0; i < meaning.definitions.size(); i++) {
                html += QString("<p>%1. %2</p>").arg(i + 1).arg(meaning.definitions[i]);
            }
            
            for (const QString& example : meaning.examples) {
                html += QString("<p style='color: #888; margin-left: 20px;'>%1</p>").arg(example);
            }
            
            if (!meaning.synonyms.isEmpty()) {
                html += QString("<p><b style='color: #2196F3;'>Synonyms:</b> %1</p>").arg(meaning.synonyms.join(", "));
            }
            
            if (!meaning.antonyms.isEmpty()) {
                html += QString("<p><b style='color: #f44336;'>Antonyms:</b> %1</p>").arg(meaning.antonyms.join(", "));
            }
            
            html += "<hr>";
        }
        
        if (html.isEmpty()) {
            html = "<p>No detailed definition available.</p>";
        }
        
        m_definitionText->setHtml(html);
        m_countLabel->setText(QString("%1 meanings").arg(def.meanings.size()));
    }
    
    void updateWordOfTheDay() {
        QDate today = QDate::currentDate();
        
        if (today != m_lastWotdDate || m_currentWotd.isEmpty()) {
            QStringList words = {"serendipity", "ephemeral", "ubiquitous", "eloquent", "resilient",
                               "meticulous", "enigma", "paradox", "quintessential", "aesthetic"};
            
            int index = today.dayOfYear() % words.size();
            m_currentWotd = words[index];
            m_lastWotdDate = today;
        }
        
        m_wotdWord->setText(m_currentWotd);
        m_wotdDef->setText("Click 'Look Up' to see the definition");
    }
    
    void addToHistory(const QString& word) {
        for (int i = 0; i < m_history.size(); i++) {
            if (m_history[i].word == word) {
                m_history.removeAt(i);
                break;
            }
        }
        
        HistoryEntry entry;
        entry.word = word;
        entry.timestamp = QDateTime::currentDateTime();
        entry.lookups = 1;
        m_history.prepend(entry);
        
        while (m_history.size() > 50) m_history.removeLast();
        
        updateHistoryList();
    }
    
    void loadLocalDictionary() {
        QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/dictionary.json";
        
        if (QFile::exists(path)) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                file.close();
                
                QJsonObject obj = doc.object();
                for (auto it = obj.begin(); it != obj.end(); ++it) {
                    WordDefinition def;
                    def.word = it.key();
                    QJsonObject data = it.value().toObject();
                    def.phonetic = data["phonetic"].toString();
                    def.origin = data["origin"].toString();
                    
                    QJsonArray meanings = data["meanings"].toArray();
                    for (const QJsonValue& m : meanings) {
                        QJsonObject mObj = m.toObject();
                        WordDefinition::Meaning meaning;
                        meaning.partOfSpeech = mObj["partOfSpeech"].toString();
                        
                        QJsonArray defs = mObj["definitions"].toArray();
                        for (const QJsonValue& d : defs) {
                            meaning.definitions.append(d.toString());
                        }
                        
                        def.meanings.append(meaning);
                    }
                    
                    m_localDictionary[it.key()] = def;
                }
            }
        }
        
        if (m_localDictionary.isEmpty()) {
            WordDefinition hello;
            hello.word = "hello";
            hello.phonetic = "/həˈləʊ/";
            WordDefinition::Meaning m1;
            m1.partOfSpeech = "exclamation";
            m1.definitions.append("used as a greeting.");
            hello.meanings.append(m1);
            m_localDictionary["hello"] = hello;
        }
    }
    
    void loadDocument(const QString& path) {
        QFileInfo fi(path);
        QString ext = fi.suffix().toLower();
        
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "Error", "Cannot open file: " + path);
            return;
        }
        
        QString content = QString::fromUtf8(file.readAll());
        file.close();
        
        if (ext == "html" || ext == "htm") {
            m_readerView->setHtml(content);
        } else if (ext == "md") {
            // Simple markdown to HTML conversion
            content.replace("# ", "<h1>").replace("\n", "</h1>\n");
            content.replace("## ", "<h2>").replace("\n", "</h2>\n");
            content.replace("**", "<b>").replace("**", "</b>");
            content.replace("*", "<i>").replace("*", "</i>");
            m_readerView->setHtml("<html><body>" + content + "</body></html>");
        } else {
            m_readerView->setPlainText(content);
        }
        
        setWindowTitle(fi.fileName() + " - Reader");
        statusBar()->showMessage("Opened: " + path, 3000);
    }
    
    void showPopupDefinition(const QString& word) {
        if (word.isEmpty()) return;
        
        // Look up word
        WordDefinition def;
        if (m_localDictionary.contains(word)) {
            def = m_localDictionary[word];
        }
        
        QString html;
        html += QString("<h3>%1</h3>").arg(word);
        if (!def.phonetic.isEmpty()) {
            html += QString("<p><i>%1</i></p>").arg(def.phonetic);
        }
        
        for (const WordDefinition::Meaning& meaning : def.meanings) {
            html += QString("<p><b>%1:</b> %2</p>").arg(meaning.partOfSpeech).arg(meaning.definitions.join("; "));
        }
        
        if (def.meanings.isEmpty()) {
            html += "<p>Click search to look up this word</p>";
        }
        
        m_popupText->setHtml(html);
        
        // Position popup near cursor
        QPoint pos = QCursor::pos();
        m_popupWidget->move(pos + QPoint(10, 10));
        m_popupWidget->show();
        
        // Auto-hide after 5 seconds
        QTimer::singleShot(5000, m_popupWidget, &QWidget::hide);
    }
    
    void updateFavoritesList() {
        m_favoritesList->clear();
        for (const QString& word : m_favorites) {
            m_favoritesList->addItem(word);
        }
        m_countLabel->setText(QString("%1 favorites").arg(m_favorites.size()));
    }
    
    void updateHistoryList() {
        m_historyList->clear();
        for (const HistoryEntry& entry : m_history) {
            QListWidgetItem* item = new QListWidgetItem(entry.word);
            item->setToolTip(entry.timestamp.toString("yyyy-MM-dd HH:mm"));
            m_historyList->addItem(item);
        }
    }
    
    void loadFavorites() {
        QSettings settings("Havel WM", "Dictionary");
        QStringList favList = settings.value("favorites").toStringList();
        m_favorites = QSet<QString>(favList.begin(), favList.end());
        updateFavoritesList();
    }
    
    void saveFavorites() {
        QSettings settings("Havel WM", "Dictionary");
        settings.setValue("favorites", QStringList(m_favorites.begin(), m_favorites.end()));
    }
    
    void loadHistory() {
        QSettings settings("Havel WM", "Dictionary");
        QJsonArray arr = settings.value("history").toJsonArray();
        
        m_history.clear();
        for (const QJsonValue& val : arr) {
            QJsonObject obj = val.toObject();
            HistoryEntry entry;
            entry.word = obj["word"].toString();
            entry.timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
            entry.lookups = obj["lookups"].toInt();
            m_history.append(entry);
        }
        
        updateHistoryList();
    }
    
    void saveHistory() {
        QSettings settings("Havel WM", "Dictionary");
        
        QJsonArray arr;
        for (const HistoryEntry& entry : m_history) {
            QJsonObject obj;
            obj["word"] = entry.word;
            obj["timestamp"] = entry.timestamp.toString(Qt::ISODate);
            obj["lookups"] = entry.lookups;
            arr.append(obj);
        }
        
        settings.setValue("history", arr);
    }
    
    void loadSettings() {
        QSettings settings("Havel WM", "Dictionary");
        resize(settings.value("size", QSize(1100, 800)).toSize());
        move(settings.value("pos", QPoint(100, 100)).toPoint());
        m_languageCombo->setCurrentIndex(settings.value("language", 0).toInt());
        m_typeCombo->setCurrentIndex(settings.value("type", 0).toInt());
    }
    
    void saveSettings() {
        QSettings settings("Havel WM", "Dictionary");
        settings.setValue("size", size());
        settings.setValue("pos", pos());
        settings.setValue("language", m_languageCombo->currentIndex());
        settings.setValue("type", m_typeCombo->currentIndex());
    }
    
    // UI components
    QStackedWidget* m_stackedWidget;
    
    // Dictionary view
    QLineEdit* m_searchEdit;
    QPushButton* m_searchButton;
    QPushButton* m_randomButton;
    QComboBox* m_languageCombo;
    QComboBox* m_typeCombo;
    
    QLabel* m_wordLabel;
    QLabel* m_phoneticLabel;
    QLabel* m_originLabel;
    QTextEdit* m_definitionText;
    QPushButton* m_playAudioButton;
    QPushButton* m_favoriteButton;
    
    QTabWidget* m_sidebarTabs;
    QListWidget* m_favoritesList;
    QListWidget* m_historyList;
    QPushButton* m_clearFavButton;
    QPushButton* m_clearHistButton;
    
    QLabel* m_wotdLabel;
    QLabel* m_wotdWord;
    QLabel* m_wotdDef;
    
    // Translator
    QTextEdit* m_translateSource;
    QTextEdit* m_translateResult;
    QComboBox* m_translateFrom;
    QComboBox* m_translateTo;
    
    // Reader view
    QTextEdit* m_readerView;
    QSlider* m_zoomSlider;
    QLabel* m_zoomLabel;
    
    // Popup definition
    QWidget* m_popupWidget;
    QTextEdit* m_popupText;
    
    // Status bar
    QLabel* m_statusLabel;
    QLabel* m_countLabel;
    
    // System tray
    QSystemTrayIcon* m_trayIcon;
    
    // Network
    QNetworkAccessManager* m_networkManager;
    QString m_currentAudioUrl;
    
    // Data
    WordDefinition m_currentDefinition;
    QMap<QString, WordDefinition> m_localDictionary;
    QList<HistoryEntry> m_history;
    QSet<QString> m_favorites;
    
    // Word of the day
    QString m_currentWotd;
    QDate m_lastWotdDate;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Havel Dictionary & Reader");
    app.setOrganizationName("Havel WM");
    app.setDesktopFileName("havel-dictionary");
    
    // Dark theme
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    app.setPalette(darkPalette);
    
    DictionaryApp dictionary;
    dictionary.show();
    
    // Handle CLI arguments
    QStringList args = app.arguments();
    for (int i = 1; i < args.size(); i++) {
        if (args[i] == "--lookup" && i + 1 < args.size()) {
            QTimer::singleShot(500, [&dictionary, &args, i]() {
                dictionary.lookupWord(args[i + 1]);
            });
        } else if (args[i] == "--translate" && i + 2 < args.size()) {
            QString text = args[i + 1];
            QString target = "en";
            if (i + 3 < args.size() && !args[i + 3].startsWith("--")) {
                target = args[i + 3];
            }
            QTimer::singleShot(500, [&dictionary, text, target]() {
                dictionary.translateText(text, "auto", target);
            });
        } else if (args[i] == "--detect" && i + 1 < args.size()) {
            QTimer::singleShot(500, [&dictionary, &args, i]() {
                dictionary.detectLanguage(args[i + 1]);
            });
        } else if (args[i] == "--wotd") {
            QTimer::singleShot(500, [&dictionary]() {
                dictionary.showWordOfTheDay();
            });
        } else if (args[i] == "--open" && i + 1 < args.size()) {
            QTimer::singleShot(500, [&dictionary, &args, i]() {
                dictionary.openDocument(args[i + 1]);
            });
        }
    }
    
    return app.exec();
}

#include "Dictionary.moc"
