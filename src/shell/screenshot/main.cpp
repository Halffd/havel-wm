// Screenshot - Main Entry Point

#include <QApplication>
#include <QStyleFactory>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>
#include <QDebug>

#include "Screenshot.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    
    // Set application info
    app.setApplicationName("Havel Screenshot");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("Havel WM");
    
    // Use Fusion style
    app.setStyle(QStyleFactory::create("Fusion"));
    
    // Command line parsing
    QCommandLineParser parser;
    parser.setApplicationDescription("Screenshot Utility for Havel WM");
    parser.addHelpOption();
    parser.addVersionOption();
    
    // Options
    QCommandLineOption fullscreenOption(QStringList() << "f" << "fullscreen",
                                         "Capture full screen immediately");
    parser.addOption(fullscreenOption);
    
    QCommandLineOption windowOption(QStringList() << "w" << "window",
                                     "Capture active window immediately");
    parser.addOption(windowOption);
    
    QCommandLineOption regionOption(QStringList() << "r" << "region",
                                     "Capture selected region immediately");
    parser.addOption(regionOption);
    
    QCommandLineOption delayOption(QStringList() << "d" << "delay",
                                    "Delay in seconds", "seconds", "0");
    parser.addOption(delayOption);
    
    QCommandLineOption clipboardOption(QStringList() << "c" << "clipboard",
                                        "Copy to clipboard instead of saving");
    parser.addOption(clipboardOption);
    
    parser.process(app);
    
    // Create main window
    havel::ScreenshotWindow window;
    
    // Handle command line options
    if (parser.isSet(fullscreenOption)) {
        QTimer::singleShot(500, &window, [&window]() {
            window.onCaptureFullScreen();
        });
    } else if (parser.isSet(windowOption)) {
        QTimer::singleShot(500, &window, [&window]() {
            window.onCaptureWindow();
        });
    } else if (parser.isSet(regionOption)) {
        QTimer::singleShot(500, &window, [&window]() {
            window.onCaptureRegion();
        });
    } else {
        window.show();
    }
    
    return app.exec();
}
