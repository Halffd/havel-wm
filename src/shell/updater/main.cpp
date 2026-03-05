// System Updater - Main Entry Point

#include <QApplication>
#include <QStyleFactory>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>
#include <QDebug>
#include <iostream>

#include "SystemUpdater.hpp"

int main(int argc, char* argv[]) {
    // Check for CLI arguments first
    QStringList args;
    for (int i = 1; i < argc; i++) {
        args << QString::fromLocal8Bit(argv[i]);
    }
    
    // CLI mode
    if (args.contains("--check") || args.contains("--install") || 
        args.contains("--list") || args.contains("--history")) {
        
        QCoreApplication app(argc, argv);
        app.setApplicationName("Havel System Updater");
        app.setOrganizationName("Havel WM");
        
        havel::SystemUpdater updater;
        
        for (int i = 0; i < args.size(); i++) {
            if (args[i] == "--check") {
                std::cout << "Checking for updates..." << std::endl;
                updater.checkUpdates();
            }
            else if (args[i] == "--install" && i + 1 < args.size()) {
                QString package = args[i + 1];
                std::cout << "Installing " << package.toStdString() << "..." << std::endl;
                updater.installPackage(package);
            }
            else if (args[i] == "--install-all") {
                std::cout << "Installing all updates..." << std::endl;
                updater.installUpdates();
            }
            else if (args[i] == "--list") {
                std::cout << "Available updates:" << std::endl;
                updater.listUpdates();
            }
            else if (args[i] == "--history") {
                std::cout << "Update history:" << std::endl;
                updater.showHistory();
            }
            else if (args[i] == "--set" && i + 2 < args.size()) {
                QString key = args[i + 1];
                QString value = args[i + 2];
                updater.setSettings(key, value);
                std::cout << "Set " << key.toStdString() << " = " << value.toStdString() << std::endl;
            }
        }
        
        return 0;
    }
    
    // GUI mode
    QApplication app(argc, argv);
    app.setApplicationName("Havel System Updater");
    app.setOrganizationName("Havel WM");
    app.setDesktopFileName("havel-updater");
    
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
    
    havel::SystemUpdater updater;
    updater.show();
    
    return app.exec();
}
