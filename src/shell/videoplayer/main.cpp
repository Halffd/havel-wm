// Video Player - Main Entry Point

#include <QApplication>
#include <QStyleFactory>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>
#include <QDebug>
#include <iostream>

#include "VideoPlayer.hpp"

int main(int argc, char* argv[]) {
    // Check for CLI arguments first
    QStringList args;
    for (int i = 1; i < argc; i++) {
        args << QString::fromLocal8Bit(argv[i]);
    }
    
    // CLI mode
    if (args.contains("--play") || args.contains("--add") || 
        args.contains("--volume") || args.contains("--fullscreen")) {
        
        QCoreApplication app(argc, argv);
        app.setApplicationName("Havel Video Player");
        app.setOrganizationName("Havel WM");
        
        havel::VideoPlayer player;
        player.show();
        
        for (int i = 0; i < args.size(); i++) {
            if (args[i] == "--play" && i + 1 < args.size()) {
                player.playFile(args[i + 1]);
            }
            else if (args[i] == "--add" && i + 1 < args.size()) {
                player.addToPlaylist(args[i + 1]);
            }
            else if (args[i] == "--volume" && i + 1 < args.size()) {
                player.setVolume(args[i + 1].toInt());
            }
            else if (args[i] == "--fullscreen") {
                player.setFullscreen(true);
            }
            else if (args[i] == "--playlist") {
                player.showPlaylist();
            }
        }
        
        return app.exec();
    }
    
    // GUI mode
    QApplication app(argc, argv);
    app.setApplicationName("Havel Video Player");
    app.setOrganizationName("Havel WM");
    app.setDesktopFileName("havel-videoplayer");
    
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
    
    havel::VideoPlayer player;
    player.show();
    
    return app.exec();
}
