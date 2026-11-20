// File Manager - Main Entry Point

#include <QApplication>
#include <QStyleFactory>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>
#include <QDebug>

#include "FileManager.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    
    // Set application info
    app.setApplicationName("Havel File Manager");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("Havel WM");
    
    // Use Fusion style for consistent look
    app.setStyle(QStyleFactory::create("Fusion"));
    
    // Command line parsing
    QCommandLineParser parser;
    parser.setApplicationDescription("Havel WM File Manager");
    parser.addHelpOption();
    parser.addVersionOption();
    
    // Start path argument
    parser.addPositionalArgument("path", "Start path", "[path]");
    
    // Options
    QCommandLineOption newWindowOption(QStringList() << "n" << "new-window", "Open in new window");
    parser.addOption(newWindowOption);
    
    parser.process(app);
    
    // Get start path
    QString startPath;
    QStringList positionalArgs = parser.positionalArguments();
    if (!positionalArgs.isEmpty()) {
        startPath = positionalArgs.first();
        if (!QDir(startPath).exists()) {
            qWarning() << "Path does not exist:" << startPath;
            startPath = QDir::homePath();
        }
    }
    
    // Create and show main window
    havel::FileManagerWindow window(startPath);
    window.show();
    
    return app.exec();
}
