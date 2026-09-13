#include "app/appearance_controller.h"
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
    args.addOption({"screenshot-theme", "Theme for a screenshot (system, light, or dark).", "theme",
                    "system"});
    args.addOption({"screenshot-size", "Window size for a screenshot, for example 960x640.", "size",
                    "1280x900"});
    args.process(app);
    const auto storagePath =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath("choscordb.sqlite");
    choscordb::MainWindow window(nullptr, storagePath);
    const auto applyScreenshotOptions = [&] {
        const auto requestedTheme = args.value("screenshot-theme");
        if (auto* appearance = window.findChild<choscordb::AppearanceController*>())
            (void)appearance->preview(requestedTheme);
        const auto dimensions = args.value("screenshot-size").split('x');
        if (dimensions.size() == 2) {
            bool widthOk = false, heightOk = false;
            const int width = dimensions[0].toInt(&widthOk),
                      height = dimensions[1].toInt(&heightOk);
            if (widthOk && heightOk)
                window.resize(width, height);
        }
    };
    window.show();
    if (args.isSet("screenshot")) {
        QTimer::singleShot(400, &window, applyScreenshotOptions);
        QTimer::singleShot(700, &app,
                           [&] { app.exit(window.grab().save(args.value("screenshot")) ? 0 : 1); });
    } else if (args.isSet("smoke-test"))
        QTimer::singleShot(100, &app, &QApplication::quit);
    return app.exec();
}
