// Simple Game Launcher for Havel WM

#include <QApplication>
#include <QMainWindow>
#include <QListWidget>
#include <QListWidgetItem>
#include <QToolBar>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QSystemTrayIcon>
#include <QCloseEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDateTime>
#include <QTimer>
#include <QInputDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QScrollArea>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QClipboard>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QIcon>
#include <QStyleFactory>

struct Game {
    QString id;
    QString name;
    QString executable;
    QString workingDir;
    QString prefix;
    QString runner;
    QString coverImage;
    bool favorite;
    bool hidden;
    QDateTime lastPlayed;
    int playTime;
    
    Game() : favorite(false), hidden(false), playTime(0) {}
};

class GameLauncher : public QMainWindow {
    Q_OBJECT

public:
    GameLauncher() {
        setWindowTitle("Game Launcher - Havel WM");
        setMinimumSize(1000, 700);
        
        // Central widget with splitter
        QSplitter* splitter = new QSplitter(Qt::Horizontal);
        setCentralWidget(splitter);
        
        // Game list
        m_gameList = new QListWidget();
        m_gameList->setMinimumWidth(250);
        m_gameList->setViewMode(QListView::ListMode);
        connect(m_gameList, &QListWidget::itemClicked, this, &GameLauncher::onGameSelected);
        connect(m_gameList, &QListWidget::itemDoubleClicked, this, &GameLauncher::onGameActivated);
        splitter->addWidget(m_gameList);
        
        // Details panel
        QWidget* detailsPanel = new QWidget();
        QVBoxLayout* detailsLayout = new QVBoxLayout(detailsPanel);
        
        // Cover and info
        QHBoxLayout* topLayout = new QHBoxLayout();
        
        m_coverLabel = new QLabel();
        m_coverLabel->setFixedSize(150, 200);
        m_coverLabel->setAlignment(Qt::AlignCenter);
        m_coverLabel->setStyleSheet("QLabel { background-color: #202020; border-radius: 8px; }");
        topLayout->addWidget(m_coverLabel);
        
        QVBoxLayout* infoLayout = new QVBoxLayout();
        m_nameLabel = new QLabel("Select a game");
        QFont font = m_nameLabel->font();
        font.setPointSize(16);
        font.setBold(true);
        m_nameLabel->setFont(font);
        infoLayout->addWidget(m_nameLabel);
        
        m_runnerLabel = new QLabel("Runner: -");
        infoLayout->addWidget(m_runnerLabel);
        
        m_lastPlayedLabel = new QLabel("Last played: Never");
        infoLayout->addWidget(m_lastPlayedLabel);
        
        m_playTimeLabel = new QLabel("Play time: 0 min");
        infoLayout->addWidget(m_playTimeLabel);
        
        infoLayout->addStretch();
        topLayout->addLayout(infoLayout);
        detailsLayout->addLayout(topLayout);
        
        // Description
        m_descriptionEdit = new QTextEdit();
        m_descriptionEdit->setReadOnly(true);
        m_descriptionEdit->setMaximumHeight(80);
        m_descriptionEdit->setPlaceholderText("Description");
        detailsLayout->addWidget(m_descriptionEdit);
        
        // Buttons
        QHBoxLayout* buttonLayout = new QHBoxLayout();
        
        m_playButton = new QPushButton("▶ Play");
        m_playButton->setMinimumHeight(40);
        m_playButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; font-size: 14px; border-radius: 4px; }");
        connect(m_playButton, &QPushButton::clicked, this, &GameLauncher::onPlayClicked);
        buttonLayout->addWidget(m_playButton);
        
        m_settingsButton = new QPushButton("⚙ Settings");
        connect(m_settingsButton, &QPushButton::clicked, this, &GameLauncher::onSettingsClicked);
        buttonLayout->addWidget(m_settingsButton);
        
        m_prefixButton = new QPushButton("📁 Prefix");
        connect(m_prefixButton, &QPushButton::clicked, this, &GameLauncher::onPrefixClicked);
        buttonLayout->addWidget(m_prefixButton);
        
        m_logsButton = new QPushButton("📋 Logs");
        connect(m_logsButton, &QPushButton::clicked, this, &GameLauncher::onLogsClicked);
        buttonLayout->addWidget(m_logsButton);
        
        detailsLayout->addLayout(buttonLayout);
        detailsLayout->addStretch();
        
        splitter->addWidget(detailsPanel);
        splitter->setSizes(QList<int>() << 300 << 700);
        
        // Status bar
        m_statusLabel = new QLabel("0 games");
        statusBar()->addWidget(m_statusLabel);
        
        setupMenu();
        setupToolbar();
        loadGames();
        loadSettings();
        
        updateGameList();
        updateStatus();
        
        // System tray
        m_trayIcon = new QSystemTrayIcon(QIcon::fromTheme("applications-games"), this);
        m_trayIcon->show();
        
        QMenu* trayMenu = new QMenu(this);
        trayMenu->addAction("Show", this, &GameLauncher::show);
        trayMenu->addAction("Quit", qApp, &QApplication::quit);
        m_trayIcon->setContextMenu(trayMenu);
        
        connect(m_trayIcon, &QSystemTrayIcon::activated, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::DoubleClick) {
                show();
                raise();
                activateWindow();
            }
        });
    }
    
    ~GameLauncher() {
        saveGames();
        saveSettings();
    }
    
    // CLI support
    void launchGame(const QString& gameId) {
        for (const Game& game : m_games) {
            if (game.id == gameId) {
                launchGameInternal(game);
                return;
            }
        }
        QMessageBox::critical(this, "Error", "Game not found: " + gameId);
    }
    
    void addGame(const Game& game) {
        m_games.append(game);
        updateGameList();
        saveGames();
    }
    
    void removeGame(const QString& gameId) {
        for (int i = 0; i < m_games.size(); i++) {
            if (m_games[i].id == gameId) {
                m_games.removeAt(i);
                updateGameList();
                saveGames();
                return;
            }
        }
    }
    
    void importFromSteam() {
        QMessageBox::information(this, "Import Steam", "Steam import would scan ~/.steam/steam for installed games");
        // Would scan Steam library
    }
    
    void importFromLutris() {
        QMessageBox::information(this, "Import Lutris", "Lutris import would scan ~/.config/lutris/games for installed games");
        // Would scan Lutris library
    }
    
private slots:
    void onNewGame() {
        Game game;
        game.id = "game_" + QString::number(QDateTime::currentMSecsSinceEpoch());
        game.name = "New Game";
        game.runner = "native";
        
        bool ok;
        game.name = QInputDialog::getText(this, "New Game", "Game name:", QLineEdit::Normal, game.name, &ok);
        if (ok && !game.name.isEmpty()) {
            addGame(game);
        }
    }
    
    void onImportGames() {
        QMenu menu(this);
        menu.addAction("Import from Steam", this, &GameLauncher::importFromSteam);
        menu.addAction("Import from Lutris", this, &GameLauncher::importFromLutris);
        menu.exec(QCursor::pos());
    }
    
    void onExportLibrary() {
        QString path = QFileDialog::getSaveFileName(this, "Export Library", "", "JSON Files (*.json)");
        if (!path.isEmpty()) {
            QJsonArray array;
            for (const Game& game : m_games) {
                QJsonObject obj;
                obj["id"] = game.id;
                obj["name"] = game.name;
                obj["executable"] = game.executable;
                obj["workingDir"] = game.workingDir;
                obj["prefix"] = game.prefix;
                obj["runner"] = game.runner;
                obj["favorite"] = game.favorite;
                obj["hidden"] = game.hidden;
                array.append(obj);
            }
            
            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QJsonDocument(array).toJson());
                file.close();
            }
        }
    }
    
    void onImportLibrary() {
        QString path = QFileDialog::getOpenFileName(this, "Import Library", "", "JSON Files (*.json)");
        if (!path.isEmpty()) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                file.close();
                
                m_games.clear();
                for (const QJsonValue& val : doc.array()) {
                    QJsonObject obj = val.toObject();
                    Game game;
                    game.id = obj["id"].toString();
                    game.name = obj["name"].toString();
                    game.executable = obj["executable"].toString();
                    game.workingDir = obj["workingDir"].toString();
                    game.prefix = obj["prefix"].toString();
                    game.runner = obj["runner"].toString();
                    game.favorite = obj["favorite"].toBool();
                    game.hidden = obj["hidden"].toBool();
                    m_games.append(game);
                }
                updateGameList();
                saveGames();
            }
        }
    }
    
    void onPlayClicked() {
        QListWidgetItem* item = m_gameList->currentItem();
        if (!item) return;
        
        int index = m_gameList->row(item);
        if (index >= 0 && index < m_games.size()) {
            launchGameInternal(m_games[index]);
        }
    }
    
    void onSettingsClicked() {
        QListWidgetItem* item = m_gameList->currentItem();
        if (!item) return;
        
        int index = m_gameList->row(item);
        if (index >= 0 && index < m_games.size()) {
            Game& game = m_games[index];
            
            QDialog dialog(this);
            dialog.setWindowTitle("Game Settings - " + game.name);
            dialog.setMinimumSize(400, 400);
            
            QFormLayout* layout = new QFormLayout(&dialog);
            
            QLineEdit* nameEdit = new QLineEdit(game.name);
            layout->addRow("Name:", nameEdit);
            
            QLineEdit* exeEdit = new QLineEdit(game.executable);
            QPushButton* browseExe = new QPushButton("...");
            connect(browseExe, &QPushButton::clicked, [exeEdit, this]() {
                QString path = QFileDialog::getOpenFileName(this, "Select Executable");
                if (!path.isEmpty()) exeEdit->setText(path);
            });
            QHBoxLayout* exeLayout = new QHBoxLayout();
            exeLayout->addWidget(exeEdit);
            exeLayout->addWidget(browseExe);
            layout->addRow("Executable:", exeLayout);
            
            QLineEdit* dirEdit = new QLineEdit(game.workingDir);
            QPushButton* browseDir = new QPushButton("...");
            connect(browseDir, &QPushButton::clicked, [dirEdit, this]() {
                QString path = QFileDialog::getExistingDirectory(this, "Select Directory");
                if (!path.isEmpty()) dirEdit->setText(path);
            });
            QHBoxLayout* dirLayout = new QHBoxLayout();
            dirLayout->addWidget(dirEdit);
            dirLayout->addWidget(browseDir);
            layout->addRow("Working Dir:", dirLayout);
            
            QLineEdit* prefixEdit = new QLineEdit(game.prefix);
            QPushButton* browsePrefix = new QPushButton("...");
            connect(browsePrefix, &QPushButton::clicked, [prefixEdit, this]() {
                QString path = QFileDialog::getExistingDirectory(this, "Select Prefix");
                if (!path.isEmpty()) prefixEdit->setText(path);
            });
            QHBoxLayout* prefixLayout = new QHBoxLayout();
            prefixLayout->addWidget(prefixEdit);
            prefixLayout->addWidget(browsePrefix);
            layout->addRow("Wine Prefix:", prefixLayout);
            
            QComboBox* runnerCombo = new QComboBox();
            runnerCombo->addItems(QStringList() << "native" << "wine" << "proton" << "dosbox" << "retroarch");
            runnerCombo->setCurrentText(game.runner);
            layout->addRow("Runner:", runnerCombo);
            
            QCheckBox* favoriteCheck = new QCheckBox("Favorite");
            favoriteCheck->setChecked(game.favorite);
            layout->addRow("", favoriteCheck);
            
            QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            connect(buttons, &QDialogButtonBox::accepted, [&dialog, &game, nameEdit, exeEdit, dirEdit, prefixEdit, runnerCombo, favoriteCheck]() {
                game.name = nameEdit->text();
                game.executable = exeEdit->text();
                game.workingDir = dirEdit->text();
                game.prefix = prefixEdit->text();
                game.runner = runnerCombo->currentText();
                game.favorite = favoriteCheck->isChecked();
                dialog.accept();
            });
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            layout->addRow("", buttons);
            
            if (dialog.exec() == QDialog::Accepted) {
                updateGameList();
                saveGames();
            }
        }
    }
    
    void onPrefixClicked() {
        QListWidgetItem* item = m_gameList->currentItem();
        if (!item) return;
        
        int index = m_gameList->row(item);
        if (index >= 0 && index < m_games.size() && !m_games[index].prefix.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_games[index].prefix));
        }
    }
    
    void onLogsClicked() {
        QListWidgetItem* item = m_gameList->currentItem();
        if (!item) return;
        
        int index = m_gameList->row(item);
        if (index >= 0 && index < m_games.size()) {
            Game& game = m_games[index];
            
            QDialog dialog(this);
            dialog.setWindowTitle("Logs - " + game.name);
            dialog.setMinimumSize(600, 400);
            
            QVBoxLayout* layout = new QVBoxLayout(&dialog);
            
            QPlainTextEdit* logEdit = new QPlainTextEdit();
            logEdit->setReadOnly(true);
            logEdit->setFont(QFont("Monospace", 10));
            layout->addWidget(logEdit);
            
            QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs/" + game.id + ".log";
            if (QFile::exists(logPath)) {
                QFile file(logPath);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    logEdit->setPlainText(QTextStream(&file).readAll());
                    file.close();
                }
            } else {
                logEdit->setPlainText("No logs available");
            }
            
            QHBoxLayout* buttonLayout = new QHBoxLayout();
            
            QPushButton* refreshBtn = new QPushButton("Refresh");
            connect(refreshBtn, &QPushButton::clicked, [&logEdit, logPath]() {
                if (QFile::exists(logPath)) {
                    QFile file(logPath);
                    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        logEdit->setPlainText(QTextStream(&file).readAll());
                        file.close();
                    }
                }
            });
            buttonLayout->addWidget(refreshBtn);
            
            QPushButton* clearBtn = new QPushButton("Clear");
            connect(clearBtn, &QPushButton::clicked, [logPath, logEdit]() {
                if (QFile::exists(logPath)) {
                    QFile::remove(logPath);
                    logEdit->clear();
                }
            });
            buttonLayout->addWidget(clearBtn);
            
            QPushButton* copyBtn = new QPushButton("Copy");
            connect(copyBtn, &QPushButton::clicked, [logEdit]() {
                QApplication::clipboard()->setText(logEdit->toPlainText());
            });
            buttonLayout->addWidget(copyBtn);
            
            QPushButton* closeBtn = new QPushButton("Close");
            connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
            buttonLayout->addWidget(closeBtn);
            
            layout->addLayout(buttonLayout);
            dialog.exec();
        }
    }
    
    void onGameSelected() {
        QListWidgetItem* item = m_gameList->currentItem();
        if (!item) return;
        
        int index = m_gameList->row(item);
        if (index >= 0 && index < m_games.size()) {
            const Game& game = m_games[index];
            
            m_nameLabel->setText(game.name);
            m_runnerLabel->setText("Runner: " + game.runner);
            
            if (game.lastPlayed.isValid()) {
                m_lastPlayedLabel->setText("Last played: " + game.lastPlayed.toString("yyyy-MM-dd HH:mm"));
            } else {
                m_lastPlayedLabel->setText("Last played: Never");
            }
            
            m_playTimeLabel->setText(QString("Play time: %1 min").arg(game.playTime));
            
            if (!game.coverImage.isEmpty() && QFile::exists(game.coverImage)) {
                m_coverLabel->setPixmap(QPixmap(game.coverImage).scaled(150, 200, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            } else {
                m_coverLabel->clear();
                m_coverLabel->setText("No Cover");
            }
            
            m_playButton->setEnabled(true);
            m_settingsButton->setEnabled(true);
            m_prefixButton->setEnabled(!game.prefix.isEmpty());
        }
    }
    
    void onGameActivated(QListWidgetItem* item) {
        Q_UNUSED(item);
        onPlayClicked();
    }
    
    void onSearchTextChanged(const QString& text) {
        m_searchFilter = text;
        updateGameList();
    }
    
    void onRunnerFilterChanged(const QString& runner) {
        m_runnerFilter = runner;
        updateGameList();
    }
    
protected:
    void closeEvent(QCloseEvent* event) {
        if (m_trayIcon->isVisible()) {
            hide();
            event->ignore();
        } else {
            saveSettings();
            event->accept();
        }
    }
    
private:
    void setupMenu() {
        QMenu* fileMenu = menuBar()->addMenu("&File");
        fileMenu->addAction("&New Game", this, &GameLauncher::onNewGame, QKeySequence::New);
        fileMenu->addAction("&Import Games", this, &GameLauncher::onImportGames);
        fileMenu->addSeparator();
        fileMenu->addAction("&Export Library", this, &GameLauncher::onExportLibrary);
        fileMenu->addAction("&Import Library", this, &GameLauncher::onImportLibrary);
        fileMenu->addSeparator();
        fileMenu->addAction("E&xit", qApp, &QApplication::quit, QKeySequence::Quit);
        
        QMenu* viewMenu = menuBar()->addMenu("&View");
        viewMenu->addAction("&Refresh", this, &GameLauncher::updateGameList, QKeySequence::Refresh);
        
        QMenu* helpMenu = menuBar()->addMenu("&Help");
        helpMenu->addAction("&About", [this]() {
            QMessageBox::about(this, "About Game Launcher",
                "Havel WM Game Launcher\n\n"
                "A simple game launcher with:\n"
                "- Multiple runner support\n"
                "- Steam/Lutris integration\n"
                "- Log viewing\n"
                "- Prefix management\n"
                "- CLI support");
        });
    }
    
    void setupToolbar() {
        QToolBar* toolbar = addToolBar("Main");
        toolbar->addAction("New", this, &GameLauncher::onNewGame);
        toolbar->addAction("Import", this, &GameLauncher::onImportGames);
        toolbar->addSeparator();
        
        m_searchEdit = new QLineEdit();
        m_searchEdit->setPlaceholderText("Search games...");
        m_searchEdit->setMaximumWidth(200);
        connect(m_searchEdit, &QLineEdit::textChanged, this, &GameLauncher::onSearchTextChanged);
        toolbar->addWidget(m_searchEdit);
        
        m_runnerFilterCombo = new QComboBox();
        m_runnerFilterCombo->addItem("All Runners", "");
        m_runnerFilterCombo->addItems(QStringList() << "native" << "wine" << "proton" << "dosbox" << "retroarch");
        m_runnerFilterCombo->setMaximumWidth(150);
        connect(m_runnerFilterCombo, QOverload<const QString&>::of(&QComboBox::currentTextChanged), this, &GameLauncher::onRunnerFilterChanged);
        toolbar->addWidget(m_runnerFilterCombo);
    }
    
    void loadGames() {
        QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/games.json";
        
        if (QFile::exists(path)) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                file.close();
                
                m_games.clear();
                for (const QJsonValue& val : doc.array()) {
                    QJsonObject obj = val.toObject();
                    Game game;
                    game.id = obj["id"].toString();
                    game.name = obj["name"].toString();
                    game.executable = obj["executable"].toString();
                    game.workingDir = obj["workingDir"].toString();
                    game.prefix = obj["prefix"].toString();
                    game.runner = obj["runner"].toString();
                    game.favorite = obj["favorite"].toBool();
                    game.hidden = obj["hidden"].toBool();
                    m_games.append(game);
                }
            }
        }
    }
    
    void saveGames() {
        QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(path);
        path += "/games.json";
        
        QJsonArray array;
        for (const Game& game : m_games) {
            QJsonObject obj;
            obj["id"] = game.id;
            obj["name"] = game.name;
            obj["executable"] = game.executable;
            obj["workingDir"] = game.workingDir;
            obj["prefix"] = game.prefix;
            obj["runner"] = game.runner;
            obj["favorite"] = game.favorite;
            obj["hidden"] = game.hidden;
            array.append(obj);
        }
        
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(array).toJson());
            file.close();
        }
    }
    
    void loadSettings() {
        QSettings settings("Havel WM", "GameLauncher");
        resize(settings.value("size", QSize(1000, 700)).toSize());
        move(settings.value("pos", QPoint(100, 100)).toPoint());
    }
    
    void saveSettings() {
        QSettings settings("Havel WM", "GameLauncher");
        settings.setValue("size", size());
        settings.setValue("pos", pos());
    }
    
    void updateGameList() {
        m_gameList->clear();
        
        for (const Game& game : m_games) {
            if (game.hidden) continue;
            if (!m_searchFilter.isEmpty() && !game.name.contains(m_searchFilter, Qt::CaseInsensitive)) continue;
            if (!m_runnerFilter.isEmpty() && game.runner != m_runnerFilter) continue;
            
            QListWidgetItem* item = new QListWidgetItem(game.name + (game.favorite ? " ⭐" : ""));
            item->setIcon(QIcon::fromTheme("applications-games"));
            m_gameList->addItem(item);
        }
        
        updateStatus();
    }
    
    void updateStatus() {
        m_statusLabel->setText(QString("%1 games").arg(m_games.size()));
    }
    
    void launchGameInternal(const Game& game) {
        if (game.executable.isEmpty()) {
            QMessageBox::warning(this, "Warning", "No executable set for " + game.name);
            return;
        }
        
        // Update last played
        for (Game& g : m_games) {
            if (g.id == game.id) {
                g.lastPlayed = QDateTime::currentDateTime();
                g.playTime += 1;  // Would track actual time
                break;
            }
        }
        saveGames();
        
        // Launch game
        QProcess* process = new QProcess(this);
        
        if (!game.workingDir.isEmpty()) {
            process->setWorkingDirectory(game.workingDir);
        }
        
        if (!game.prefix.isEmpty()) {
            // Wine/Proton
            process->setEnvironment(QStringList() << "WINEPREFIX=" + game.prefix);
            process->start("wine", QStringList() << game.executable);
        } else {
            process->start(game.executable);
        }
        
        connect(process, &QProcess::started, [this, game]() {
            statusBar()->showMessage("Launched: " + game.name);
        });
        
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [this, game](int exitCode, QProcess::ExitStatus status) {
            Q_UNUSED(exitCode);
            Q_UNUSED(status);
            statusBar()->showMessage("Game closed: " + game.name, 5000);
        });
        
        // Log output
        QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs";
        QDir().mkpath(logPath);
        logPath += "/" + game.id + ".log";
        
        QFile* logFile = new QFile(logPath);
        if (logFile->open(QIODevice::Append)) {
            connect(process, &QProcess::readyReadStandardOutput, [process, logFile]() {
                logFile->write(process->readAllStandardOutput());
            });
            connect(process, &QProcess::readyReadStandardError, [process, logFile]() {
                logFile->write(process->readAllStandardError());
            });
        }
    }
    
    QListWidget* m_gameList;
    QLabel* m_coverLabel;
    QLabel* m_nameLabel;
    QLabel* m_runnerLabel;
    QLabel* m_lastPlayedLabel;
    QLabel* m_playTimeLabel;
    QTextEdit* m_descriptionEdit;
    QPushButton* m_playButton;
    QPushButton* m_settingsButton;
    QPushButton* m_prefixButton;
    QPushButton* m_logsButton;
    QLabel* m_statusLabel;
    QLineEdit* m_searchEdit;
    QComboBox* m_runnerFilterCombo;
    QSystemTrayIcon* m_trayIcon;
    
    QList<Game> m_games;
    QString m_searchFilter;
    QString m_runnerFilter;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Havel Game Launcher");
    app.setOrganizationName("Havel WM");
    
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
    
    GameLauncher launcher;
    launcher.show();
    
    // Handle CLI arguments
    QStringList args = app.arguments();
    for (int i = 1; i < args.size(); i++) {
        if (args[i] == "--launch" && i + 1 < args.size()) {
            QTimer::singleShot(500, [&launcher, &args, i]() {
                launcher.launchGame(args[i + 1]);
            });
        } else if (args[i] == "--import-steam") {
            QTimer::singleShot(500, [&launcher]() {
                launcher.importFromSteam();
            });
        } else if (args[i] == "--import-lutris") {
            QTimer::singleShot(500, [&launcher]() {
                launcher.importFromLutris();
            });
        }
    }
    
    return app.exec();
}

#include "GameLauncher.moc"
