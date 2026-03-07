#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include "PanelWindow.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("havel-panel");
    app.setApplicationVersion("0.1.0");
    
    QCommandLineParser parser;
    parser.setApplicationDescription("Havel WM Panel - Taskbar");
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption socketOption(
        QStringList() << "s" << "socket",
        "IPC socket path",
        "path",
        "/tmp/havel-ipc.sock"
    );
    parser.addOption(socketOption);
    
    parser.process(app);
    
    QString socketPath = parser.value(socketOption);
    
    // Create and show panel
    havel::PanelWindow panel;
    panel.show();

    // Try to connect to compositor IPC
    // Note: This will fail if compositor isn't running - that's OK
    // Panel will work in standalone mode (clock, launcher, etc.)
    if (!panel.connectToCompositor(socketPath)) {
        // Silent failure - panel works without compositor
        // Just show clock and launcher, no window list
        panel.showStandaloneMode();
    }

    return app.exec();
}
