#include "app/appearance_controller.h"
#include "app/application_data.h"
#include "app/diagnostics_service.h"
#include "app/main_window.h"
#include "app/updater.h"
#ifdef CHOSCORDB_CROSS_PLATFORM_UPDATER
#include "app/update_readiness.h"
#include "app/workspace_recovery.h"
#endif
#include "choscordb-bridge/src/lib.rs.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>
#include <QtGlobal>
#ifdef CHOSCORDB_CROSS_PLATFORM_UPDATER
#include <QtConcurrentRun>
#endif
#include <chrono>
#include <future>
int main(int argc, char** argv) {
    const auto askpass = choscordb::ssh_askpass_exit_code();
    if (askpass >= 0)
        return askpass;
    QApplication app(argc, argv);
    QApplication::setApplicationName("ChoscorDB");
    QApplication::setOrganizationName(CHOSCORDB_APP_ID);
    QApplication::setApplicationVersion(CHOSCORDB_VERSION);
#ifdef CHOSCORDB_CROSS_PLATFORM_UPDATER
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--apply-update"))
        return choscordb::runNativeUpdateHelper(app.arguments());
#endif
    QCommandLineParser args;
    args.addHelpOption();
    args.addVersionOption();
    args.addOption({"smoke-test", "Launch and exit through the Qt event loop."});
    args.addOption({"screenshot", "Save a workspace screenshot then exit.", "path"});
    args.addOption({"screenshot-theme", "Theme for a screenshot (system, light, or dark).", "theme",
                    "system"});
    args.addOption({"screenshot-size", "Window size for a screenshot, for example 960x640.", "size",
                    "1280x900"});
    args.process(app);
    const auto testDataDirectory =
        args.isSet("smoke-test") ? qEnvironmentVariable("CHOSCORDB_TEST_DATA_DIR") : QString{};
    const auto dataDirectory =
        testDataDirectory.isEmpty() ? choscordb::applicationDataDirectory() : testDataDirectory;
    // The application has a release version but no distinct build identifier.
    choscordb::DiagnosticsService diagnostics(dataDirectory, app.applicationVersion());
    // Filesystem initialization may wait on another process's diagnostics lock.
    // Keep that work off the Qt event loop while early typed records queue in Rust.
    std::future<bool> diagnosticsStartup;
    try {
        diagnosticsStartup =
            std::async(std::launch::async, [&diagnostics] { return diagnostics.start(); });
    } catch (...) {
        // Report the startup failure after the window is constructed.
    }
    choscordb::DiagnosticsWatchdog watchdog(&app, &diagnostics);
    const auto storagePath = QDir(dataDirectory).filePath("choscordb.sqlite");
    choscordb::MainWindow window(nullptr, storagePath, &diagnostics);
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
    if (!diagnosticsStartup.valid()) {
        window.disableDiagnostics();
        window.showStatus(QObject::tr("Local diagnostics could not be started."),
                          choscordb::ToastVariant::Warning, QStringLiteral("application"));
    }
    bool diagnosticsStarted = false;
    QTimer startupPoll;
    QObject::connect(&startupPoll, &QTimer::timeout, &window, [&] {
        if (!diagnosticsStartup.valid() ||
            diagnosticsStartup.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try {
            diagnosticsStarted = diagnosticsStartup.get();
        } catch (...) {
            diagnosticsStarted = false;
        }
        startupPoll.stop();
        if (diagnosticsStarted) {
            watchdog.start();
        } else {
            window.disableDiagnostics();
            window.showStatus(QObject::tr("Local diagnostics could not be started."),
                              choscordb::ToastVariant::Warning, QStringLiteral("application"));
        }
    });
    if (diagnosticsStartup.valid())
        startupPoll.start(10);
    choscordb::installNativeUpdater(window, args.isSet("smoke-test") || args.isSet("screenshot"));
#ifdef CHOSCORDB_CROSS_PLATFORM_UPDATER
    const auto readyPath = qEnvironmentVariable("CHOSCORDB_UPDATE_READY_FILE");
    if (!readyPath.isEmpty() && !args.isSet("smoke-test") && !args.isSet("screenshot")) {
        if (auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>()) {
            const auto acknowledge = [&window, recovery, readyPath] {
                if (window.isVisible() && recovery->isReady())
                    (void)QtConcurrent::run(
                        [readyPath] { (void)choscordb::writeUpdateReadinessFile(readyPath); });
            };
            QObject::connect(
                recovery, &choscordb::WorkspaceRecoveryController::restoreCompleted, &window,
                [&, acknowledge](bool) { QTimer::singleShot(0, &window, acknowledge); });
            QTimer::singleShot(0, &window, acknowledge);
        }
    }
#endif
    if (args.isSet("screenshot")) {
        QTimer::singleShot(400, &window, applyScreenshotOptions);
        QTimer::singleShot(700, &app,
                           [&] { app.exit(window.grab().save(args.value("screenshot")) ? 0 : 1); });
    } else if (args.isSet("smoke-test")) {
        bool validDelay = false;
        const int requestedDelay =
            qEnvironmentVariableIntValue("CHOSCORDB_TEST_SMOKE_DELAY_MS", &validDelay);
        // ui-budget: the packaged smoke test keeps the window open for at most ten seconds.
        QTimer::singleShot(validDelay ? qBound(100, requestedDelay, 10000) : 100, &app,
                           &QApplication::quit);
    }
    const int result = app.exec();
    startupPoll.stop();
    if (diagnosticsStartup.valid()) {
        try {
            diagnosticsStarted = diagnosticsStartup.get();
        } catch (...) {
            diagnosticsStarted = false;
        }
    }
    watchdog.stop();
    diagnostics.stop();
    if (args.isSet("smoke-test")) {
        const auto exportPath = qEnvironmentVariable("CHOSCORDB_TEST_EXPORT_PATH");
        if (!exportPath.isEmpty() &&
            (!diagnosticsStarted || !diagnostics.exportZip(exportPath).success))
            return result == 0 ? 2 : result;
    }
    return result;
}
