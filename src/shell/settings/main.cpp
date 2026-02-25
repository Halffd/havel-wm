#include <QApplication>
#include <QCommandLineParser>
#include "SettingsWindow.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("havel-settings");
    app.setApplicationVersion("0.1.0");
    
    QCommandLineParser parser;
    parser.setApplicationDescription("Havel WM Settings");
    parser.addHelpOption();
    parser.addVersionOption();
    
    parser.process(app);
    
    // Create and show settings window
    havel::SettingsWindow settings;
    settings.show();
    
    return app.exec();
}
