#include "app/main_window.h"
#include "app/updater.h"
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
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName(QStringLiteral("ChoscorDBUpdaterMenuTest"));
        QSettings().setValue(QStringLiteral("updates/backgroundConsent"), true);
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
        QVERIFY(automatic->isChecked());
        automatic->setChecked(false);
        QCOMPARE(QSettings().value(QStringLiteral("updates/backgroundConsent")).toBool(), false);
        auto* updates = qobject_cast<QMenu*>(check->parent());
        QVERIFY(updates);
        QCOMPARE(updates->title(), QString("Updates"));
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
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName(QStringLiteral("ChoscorDBUpdaterMenuConsentTest"));
        QSettings().setValue(QStringLiteral("updates/backgroundConsent"), false);
        choscordb::MainWindow window(nullptr, directory.filePath("workspace.sqlite"));
        choscordb::installNativeUpdaterForTest(window, true);
        auto* check = window.findChild<QAction*>("checkForUpdates");
        auto* install = window.findChild<QAction*>("installDownloadedUpdate");
        QVERIFY(check);
        QVERIFY(install);
        QVERIFY(install->isEnabled());
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
    }
};
QTEST_MAIN(UpdaterMenuTest)
#include "updater_menu_test.moc"
