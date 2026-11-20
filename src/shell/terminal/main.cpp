// Terminal - Main Entry Point

#include <QApplication>
#include <QStyleFactory>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>
#include <QDebug>

#include "Terminal.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    
    // Set application info
    app.setApplicationName("Havel Terminal");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("Havel WM");
    
    // Use Fusion style
    app.setStyle(QStyleFactory::create("Fusion"));
    
    // Command line parsing
    QCommandLineParser parser;
    parser.setApplicationDescription("Terminal Emulator for Havel WM");
    parser.addHelpOption();
    parser.addVersionOption();
    
    // Options
    QCommandLineOption shellOption(QStringList() << "s" << "shell",
                                    "Shell to use", "shell");
    parser.addOption(shellOption);
    
    QCommandLineOption commandOption(QStringList() << "e" << "execute",
                                      "Execute command", "command");
    parser.addOption(commandOption);
    
    QCommandLineOption tabOption(QStringList() << "t" << "tabs",
                                  "Number of tabs", "count", "1");
    parser.addOption(tabOption);
    
    parser.process(app);
    
    // Get options
    QString shell = parser.value(shellOption);
    QString command = parser.value(commandOption);
    int tabs = parser.value(tabOption).toInt();
    
    // Create main window
    havel::TerminalWindow window;
    window.show();
    
    // Create additional tabs
    for (int i = 1; i < tabs; i++) {
        window.newTab(shell, command.isEmpty() ? QStringList() : QStringList() << "-c" << command);
    }
    
    return app.exec();
}
