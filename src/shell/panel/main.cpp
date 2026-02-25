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
    
    // Connect to compositor
    if (!panel.connectToCompositor(socketPath)) {
        qWarning("Failed to connect to IPC socket: %s", qPrintable(socketPath));
        qWarning("Make sure havel compositor is running with IPC enabled");
    }
    
    return app.exec();
}
