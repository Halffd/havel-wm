// Settings Application - Main Entry Point

#include <QApplication>
#include <QStyleFactory>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>
#include <QDebug>

#include "SettingsWindow.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Havel Settings");
    app.setOrganizationName("Havel WM");
    app.setDesktopFileName("havel-settings");
    
    // Dark theme by default
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
    
    // Parse command line
    QCommandLineParser parser;
    parser.setApplicationDescription("Settings for Havel WM");
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption categoryOption(QStringList() << "c" << "category",
                                       "Open specific category", "category");
    parser.addOption(categoryOption);
    
    parser.process(app);
    
    havel::SettingsWindow settings;
    settings.show();
    
    return app.exec();
}
