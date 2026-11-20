#include <QApplication>
#include <QCommandLineParser>
#include "LockScreen.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("havel-lock");
    app.setApplicationVersion("0.1.0");
    
    QCommandLineParser parser;
    parser.setApplicationDescription("Havel WM Lock Screen");
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption lockOption(
        QStringList() << "l" << "lock",
        "Lock the screen immediately"
    );
    parser.addOption(lockOption);
    
    parser.process(app);
    
    havel::LockScreen lockScreen;
    
    // Lock immediately if requested
    if (parser.isSet(lockOption)) {
        lockScreen.lock();
    }
    
    return app.exec();
}
