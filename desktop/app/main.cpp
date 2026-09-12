#include "app/main_window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("ChoscorDB");
    QApplication::setOrganizationName("ChoscorDB");
    QCommandLineParser args;
    args.addHelpOption();
    args.addOption({"smoke-test", "Launch and exit through the Qt event loop."});
    args.addOption({"screenshot", "Save a workspace screenshot then exit.", "path"});
    args.process(app);
    const auto storagePath =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath("choscordb.sqlite");
    choscordb::MainWindow window(nullptr, storagePath);
    window.show();
    if (args.isSet("screenshot"))
        QTimer::singleShot(500, &app,
                           [&] { app.exit(window.grab().save(args.value("screenshot")) ? 0 : 1); });
    else if (args.isSet("smoke-test"))
        QTimer::singleShot(100, &app, &QApplication::quit);
    return app.exec();
}
