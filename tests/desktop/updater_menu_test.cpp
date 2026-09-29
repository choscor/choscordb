#include "app/main_window.h"
#include "app/updater.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include <QAction>
#include <QHostAddress>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class UpdaterMenuTest : public QObject {
    Q_OBJECT
  private slots:
    void isolatedRunHasNoUpdaterAndProductionMenuKeepsManualCheck() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings legacy(directory.filePath("legacy.ini"), QSettings::IniFormat);
        legacy.setValue(QStringLiteral("updates/backgroundConsent"), true);
        legacy.sync();
        QCOMPARE(legacy.status(), QSettings::NoError);
        qputenv("CHOSCORDB_TEST_UPDATE_LEGACY_INI", directory.filePath("legacy.ini").toUtf8());
        qputenv("CHOSCORDB_TEST_UPDATE_DATA_DIR", directory.filePath("update-data").toUtf8());
        choscordb::MainWindow window(nullptr, directory.filePath("workspace.sqlite"));
        choscordb::installNativeUpdater(window, true);
        QVERIFY(!window.findChild<QAction*>("checkForUpdates"));
        choscordb::installNativeUpdaterForTest(window);
        auto* check = window.findChild<QAction*>("checkForUpdates");
        auto* install = window.findChild<QAction*>("installDownloadedUpdate");
        auto* automatic = window.findChild<QAction*>("automaticUpdateChecks");
        QVERIFY(check);
        QVERIFY(install);
        QVERIFY(!install->isEnabled());
        QVERIFY(automatic);
        QCOMPARE(check->text(), QString("Check for Updates…"));
        QCOMPARE(check->menuRole(), QAction::ApplicationSpecificRole);
        QVERIFY(check->isEnabled());
        QVERIFY(automatic->isCheckable());
        QTRY_VERIFY(automatic->isEnabled());
        QTRY_VERIFY(automatic->isChecked());
        automatic->setChecked(false);
        QTRY_VERIFY([&] {
            const auto path = directory.filePath("update-data").toUtf8();
            const auto stored =
                choscordb::update_consent_load(rust::Str(path.constData(), size_t(path.size())));
            return stored.has_value && !stored.value && stored.error.empty();
        }());
        legacy.sync();
        QCOMPARE(legacy.value(QStringLiteral("updates/backgroundConsent")).toBool(), true);
        auto* updates = qobject_cast<QMenu*>(check->parent());
        QVERIFY(updates);
        QCOMPARE(updates->title(), QString("Updates"));
        qunsetenv("CHOSCORDB_TEST_UPDATE_DATA_DIR");
        qunsetenv("CHOSCORDB_TEST_UPDATE_LEGACY_INI");
    }
    void manualCheckRequiresRenewedConsentAfterDeclinedInstall() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        qputenv("CHOSCORDB_TEST_UPDATE_FEED_URL",
                QStringLiteral("http://127.0.0.1:%1/updates/feed.json")
                    .arg(server.serverPort())
                    .toLatin1());
        QSettings legacy(directory.filePath("legacy.ini"), QSettings::IniFormat);
        legacy.setValue(QStringLiteral("updates/backgroundConsent"), false);
        legacy.sync();
        QCOMPARE(legacy.status(), QSettings::NoError);
        qputenv("CHOSCORDB_TEST_UPDATE_LEGACY_INI", directory.filePath("legacy.ini").toUtf8());
        qputenv("CHOSCORDB_TEST_UPDATE_DATA_DIR", directory.filePath("update-data").toUtf8());
        choscordb::MainWindow window(nullptr, directory.filePath("workspace.sqlite"));
        choscordb::installNativeUpdaterForTest(window, true);
        auto* check = window.findChild<QAction*>("checkForUpdates");
        auto* install = window.findChild<QAction*>("installDownloadedUpdate");
        QVERIFY(check);
        QVERIFY(install);
        QVERIFY(install->isEnabled());
        auto* automatic = window.findChild<QAction*>("automaticUpdateChecks");
        QVERIFY(automatic);
        QTRY_VERIFY(automatic->isEnabled());
        QVERIFY(!automatic->isChecked());
        int prompts = 0;
        const auto declineInstall = [&] {
            QTimer::singleShot(0, &window, [&] {
                if (auto* dialog = window.findChild<choscordb::ConfirmationDialog*>()) {
                    if (dialog->windowTitle() == QStringLiteral("Install update"))
                        ++prompts;
                    dialog->done(QMessageBox::No);
                }
            });
            install->trigger();
        };
        declineInstall();
        QCOMPARE(prompts, 1);
        QByteArray requestBytes;
        QString requestLine;
        connect(&server, &QTcpServer::newConnection, &window, [&] {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, &window, [&, socket] {
                requestBytes += socket->readAll();
                if (requestBytes.contains("\r\n\r\n")) {
                    requestLine = QString::fromLatin1(requestBytes.split('\n').first()).trimmed();
                    socket->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: "
                                  "0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                }
            });
        });
        int networkErrors = 0;
        int unexpectedInstallPrompts = 0;
        QTimer dismiss;
        dismiss.setInterval(10);
        connect(&dismiss, &QTimer::timeout, &window, [&] {
            if (auto* dialog = window.findChild<choscordb::ConfirmationDialog*>()) {
                if (dialog->windowTitle() == QStringLiteral("Update check failed"))
                    ++networkErrors;
                else if (dialog->windowTitle() == QStringLiteral("Install update"))
                    ++unexpectedInstallPrompts;
                dialog->done(QMessageBox::No);
            }
        });
        dismiss.start();
        check->trigger();
        QTRY_COMPARE(requestLine, QStringLiteral("GET /updates/feed.json HTTP/1.1"));
        QTRY_COMPARE(networkErrors, 1);
        QCOMPARE(unexpectedInstallPrompts, 0);
        dismiss.stop();
        declineInstall();
        QCOMPARE(prompts, 2);
        QVERIFY(window.isEnabled());
        qunsetenv("CHOSCORDB_TEST_UPDATE_FEED_URL");
        qunsetenv("CHOSCORDB_TEST_UPDATE_DATA_DIR");
        qunsetenv("CHOSCORDB_TEST_UPDATE_LEGACY_INI");
    }
};
QTEST_MAIN(UpdaterMenuTest)
#include "updater_menu_test.moc"
