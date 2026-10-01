#include "app/main_window.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
#include "modern_ui_test.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>

void ModernUiTest::executionStripKeepsVisibleAndAccessibleTerminalState() {
    choscordb::MainWindow window;
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* run = window.findChild<QAction*>("runStatement");
    auto* summary = window.findChild<QLabel*>("executionSummary");
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(run->isEnabled());
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    editor->setText("SELECT missing FROM nowhere");
    run->trigger();
    QTRY_COMPARE(summary->property("state").toString(), QString("failed"));
    QTRY_VERIFY(summary->accessibleName().contains("failed", Qt::CaseInsensitive));
    editor->setText("SELECT 1 AS value");
    run->trigger();
    QTRY_COMPARE(summary->property("state").toString(), QString("completed"));
    QTRY_VERIFY(window.findChild<QLabel*>("executionDuration")->text().contains("ms"));
    QVERIFY(summary->accessibleName().contains("Completed"));
}

void ModernUiTest::resultFooterTracksOperationOutcome() {
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* theme = window.findChild<choscordb::design::ThemeManager*>();
    auto* footer = window.findChild<QWidget*>("sqlResultFooter");
    auto* summary = window.findChild<QLabel*>("executionSummary");
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* run = window.findChild<QAction*>("runStatement");
    QVERIFY(workspace && theme && footer && summary && tabs && run);
    auto* startFooter = window.findChild<QWidget*>("startFooter");
    QVERIFY(startFooter);
    auto* startLabel = startFooter->findChild<choscordb::design::Text*>();
    QVERIFY(startLabel);
    QCOMPARE(startLabel->font().pixelSize(), 13);
    QCOMPARE(startLabel->text(), QString("Choose a connection to get started"));
    QCOMPARE(startLabel->alignment(), Qt::AlignCenter);
    QCOMPARE(QString::fromLatin1(footer->metaObject()->className()),
             QString("choscordb::design::StatusLine"));
    QCOMPARE(footer->font().pixelSize(), 13);
    QCOMPARE(summary->font().pixelSize(), footer->font().pixelSize());
    for (const char* name : {"executionStateCompact", "executionDuration", "executionPage",
                             "executionRows", "executionVisibleSize"}) {
        auto* label = footer->findChild<QLabel*>(name);
        QVERIFY(label);
        QCOMPARE(label->font().pixelSize(), footer->font().pixelSize());
    }
    QCOMPARE(footer->palette().color(QPalette::Window), theme->resolvedTheme().colors.muted);

    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(run->isEnabled());
    QTRY_COMPARE(footer->palette().color(QPalette::Window), theme->resolvedTheme().colors.muted);
    theme->setMode(choscordb::design::ThemeMode::Dark);
    QTRY_COMPARE(footer->palette().color(QPalette::Window), theme->resolvedTheme().colors.muted);
    theme->setMode(choscordb::design::ThemeMode::Light);
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(editor);
    editor->setText("SELECT missing FROM nowhere");
    run->trigger();
    QTRY_COMPARE(summary->property("state").toString(), QString("failed"));
    QCOMPARE(footer->palette().color(QPalette::Window),
             theme->resolvedTheme().colors.dangerSurface);
    QTRY_VERIFY(summary->toolTip().contains("Failed:"));
    QVERIFY(summary->toolTip().contains("nowhere"));

    window.findChild<QAction*>("newQuery")->trigger();
    auto* other = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(other && other != editor);
    other->setConnectionTarget(std::nullopt, {});
    QTRY_COMPARE(footer->palette().color(QPalette::Window), theme->resolvedTheme().colors.muted);
    other->setConnectionTarget(editor->connectionTarget(), editor->targetLabel());
    QTRY_VERIFY(run->isEnabled());
    other->setText("SELECT 2");
    run->trigger();
    QTRY_COMPARE(summary->property("state").toString(), QString("completed"));
    tabs->setCurrentWidget(editor);
    QTRY_COMPARE(footer->palette().color(QPalette::Window),
                 theme->resolvedTheme().colors.dangerSurface);
    QVERIFY(window.findChild<QPlainTextEdit*>("queryMessages")->toPlainText().contains("nowhere"));
    QTRY_VERIFY(run->isEnabled());
    editor->setText("SELECT 1");
    run->trigger();
    QTRY_COMPARE(summary->property("state").toString(), QString("completed"));
    QCOMPARE(footer->palette().color(QPalette::Window),
             theme->resolvedTheme().colors.successSurface);
}

void ModernUiTest::connectionAttemptFailureIsAnOperationError() {
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    workspace->connectSqlite(directory.filePath("missing/database.sqlite"));
    auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
    QTRY_VERIFY(!messages->toPlainText().isEmpty());
    auto* footer = window.findChild<QWidget*>("sqlResultFooter");
    QCOMPARE(footer->palette().color(QPalette::Window),
             choscordb::design::resolvedThemeForWidget(window).colors.dangerSurface);
    QVERIFY(footer->toolTip().contains("Failed:"));
}

void ModernUiTest::sqlDocumentSwitchKeepsFooterAndResultOwnershipTogether() {
    choscordb::MainWindow window;
    window.resize(1280, 900);
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* run = window.findChild<QAction*>("runStatement");
    window.findChild<QAction*>("newQuery")->trigger();
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(run->isEnabled());
    auto* first = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    auto* grid = window.findChild<QTableView*>("queryResults");
    auto* summary = window.findChild<QLabel*>("executionSummary");
    auto* page = window.findChild<QLabel*>("executionPage");
    auto* next = window.findChild<QPushButton*>("nextPage");
    first->setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1001) "
                   "SELECT x FROM n");
    run->trigger();
    QTRY_COMPARE(grid->model()->rowCount(), 1000);
    QTRY_VERIFY(next->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* second = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(second && second != first);
    QVERIFY(!grid->isVisible());
    QVERIFY(!next->isEnabled());
    QVERIFY(!window.findChild<QPushButton*>("exportResult")->isEnabled());
    QVERIFY(page->text().isEmpty());
    tabs->setCurrentWidget(first);
    QTRY_VERIFY(grid->isVisible());
    QVERIFY(next->isEnabled());
    QCOMPARE(page->text(), QString("Page 1"));
    tabs->setCurrentWidget(second);
    second->setText("SELECT 42");
    run->trigger();
    QTRY_COMPARE(grid->model()->rowCount(), 1);
    QTRY_COMPARE(summary->property("state").toString(), QString("completed"));
    QCOMPARE(grid->model()->index(0, 0).data().toString(), QString("42"));
    tabs->setCurrentWidget(first);
    QVERIFY(!grid->isVisible());
    QVERIFY(page->text().isEmpty());
    QCOMPARE(window.findChild<QWidget*>("sqlResultFooter")->palette().color(QPalette::Window),
             choscordb::design::resolvedThemeForWidget(window).colors.muted);
    tabs->setCurrentWidget(second);
    QTRY_VERIFY(grid->isVisible());
    QCOMPARE(grid->model()->index(0, 0).data().toString(), QString("42"));
    QVERIFY(summary->toolTip().contains(second->property("documentTitle").toString()));
    const auto secondTitle = second->property("documentTitle").toString();
    second->setModified(false);
    tabs->tabCloseRequested(tabs->indexOf(second));
    QTRY_COMPARE(tabs->currentWidget(), first);
    QVERIFY(!grid->isVisible());
    const auto ownerConnection = *first->connectionTarget();
    QVERIFY(workspace->adapter()->disconnectConnection(ownerConnection));
    QTRY_COMPARE(window.findChild<QComboBox*>("connectionSelector")
                     ->findData(QVariant::fromValue<qulonglong>(ownerConnection)),
                 -1);
    QVERIFY(!grid->isVisible());
    QVERIFY(!summary->toolTip().contains(secondTitle));
}

void ModernUiTest::completedResultFooterSeparatesAndClearsMetrics() {
    choscordb::MainWindow window;
    window.resize(1200, 700);
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* run = window.findChild<QAction*>("runStatement");
    auto* source = window.findChild<QLabel*>("executionSummary");
    auto* outcome = window.findChild<QLabel*>("executionStateCompact");
    auto* duration = window.findChild<QLabel*>("executionDuration");
    auto* page = window.findChild<QLabel*>("executionPage");
    auto* rows = window.findChild<QLabel*>("executionRows");
    auto* size = window.findChild<QLabel*>("executionVisibleSize");
    auto* previous = window.findChild<QPushButton*>("previousPage");
    QVERIFY(workspace && tabs && run && source && outcome && duration && page && rows && size &&
            previous);
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(run->isEnabled());
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(editor);
    editor->setText("SELECT 1 AS value");
    run->trigger();
    QTRY_COMPARE(source->property("state").toString(), QString("completed"));
    QTRY_COMPARE(page->text(), QString("Page 1"));
    QCOMPARE(rows->text(), QString("1 rows"));
    QTRY_VERIFY(duration->text().endsWith(" ms"));
    QVERIFY(size->text().endsWith(" KiB visible"));
    QVERIFY(duration->isVisible() && page->isVisible() && rows->isVisible() && size->isVisible());
    QVERIFY(outcome->text().contains("Completed"));
    QVERIFY(source->text().contains(editor->property("documentTitle").toString()));
    QVERIFY(!source->text().contains(" ms"));
    QCoreApplication::processEvents();
    QVERIFY(source->geometry().right() < duration->geometry().left());
    QVERIFY(size->geometry().right() < previous->mapTo(size->parentWidget(), QPoint()).x());

    QTRY_VERIFY(run->isEnabled());
    editor->setText("CREATE TABLE metric_reset(value INTEGER)");
    run->trigger();
    QTRY_VERIFY(page->text().isEmpty());
    QTRY_COMPARE(source->property("state").toString(), QString("completed"));
    QTRY_VERIFY(!duration->text().isEmpty());
    QVERIFY(page->text().isEmpty());
    QVERIFY(rows->text().contains("rows affected"));
    QVERIFY(size->text().isEmpty());

    QTRY_VERIFY(run->isEnabled());
    editor->setText("INSERT INTO metric_reset(value) VALUES (1), (2) RETURNING value");
    run->trigger();
    QTRY_COMPARE(page->text(), QString("Page 1"));
    QTRY_COMPARE(rows->text(), QString("2 rows"));
}

void ModernUiTest::narrowResultFooterPreservesOutcomeNavigationAndDetails() {
    choscordb::MainWindow window;
    window.resize(960, 640);
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* run = window.findChild<QAction*>("runStatement");
    auto* footer = window.findChild<QWidget*>("sqlResultFooter");
    auto* source = window.findChild<QLabel*>("executionSummary");
    auto* outcome = window.findChild<QLabel*>("executionStateCompact");
    auto* page = window.findChild<QLabel*>("executionPage");
    auto* previous = window.findChild<QPushButton*>("previousPage");
    auto* next = window.findChild<QPushButton*>("nextPage");
    QVERIFY(workspace && tabs && run && footer && source && outcome && page && previous && next);
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(run->isEnabled());
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    std::optional<quint64> resultQuery;
    connect(workspace->adapter(), &choscordb::EngineAdapter::eventReady, &window,
            [&](const choscordb::BridgeEvent& event) {
                if (QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size())) ==
                    "stored_page")
                    resultQuery = event.id;
            });
    const QString longTitle(160, QLatin1Char('L'));
    editor->setProperty("documentTitle", longTitle);
    editor->setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1101) "
                    "SELECT x FROM n");
    run->trigger();
    QTRY_COMPARE(source->property("state").toString(), QString("completed"));
    QTRY_VERIFY(next->isEnabled());
    QCoreApplication::processEvents();
    QVERIFY2(source->text().contains(QChar(0x2026)),
             qPrintable(QString("source width=%1 text=%2 full=%3")
                            .arg(source->width())
                            .arg(source->text(), source->property("fullSource").toString())));
    QVERIFY(source->toolTip().contains(longTitle));
    QVERIFY(source->toolTip().contains("Page 1"));
    QVERIFY(source->accessibleName().contains(longTitle));
    QVERIFY(source->accessibleName().contains("KiB visible"));
    QVERIFY(outcome->isVisible());
    QVERIFY(outcome->text().contains("Completed"));
    QVERIFY(footer->rect().contains(QRect(previous->mapTo(footer, QPoint()), previous->size())));
    QVERIFY(footer->rect().contains(QRect(next->mapTo(footer, QPoint()), next->size())));
    QTest::mouseClick(next, Qt::LeftButton);
    QCOMPARE(page->text(), QString{});
    QTRY_COMPARE(page->text(), QString("Page 2"));
    QVERIFY(resultQuery.has_value());
    QTRY_VERIFY(previous->isEnabled());
    workspace->adapter()->releaseQuery(*resultQuery);
    QTest::mouseClick(previous, Qt::LeftButton);
    QTRY_COMPARE(source->property("state").toString(), QString("failed"));
    QTRY_VERIFY(outcome->text().contains("Failed"));
}

void ModernUiTest::operationFailureRemainsRedAfterDisconnectCleanup() {
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* adapter = workspace->adapter();
    std::optional<quint64> queryId;
    connect(adapter, &choscordb::EngineAdapter::eventReady, &window,
            [&queryId](const choscordb::BridgeEvent& event) {
                if (event.kind == "query_finished")
                    queryId = event.id;
            });
    workspace->connectSqlite(":memory:");
    auto* run = window.findChild<QAction*>("runStatement");
    QTRY_VERIFY(run->isEnabled());
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    editor->setText("SELECT 1");
    run->trigger();
    QTRY_VERIFY(queryId.has_value());
    const auto connection =
        window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
    // Fixture for the backend's ordered query failure, then connection cleanup.
    choscordb::BridgeEvent failure{};
    failure.kind = "query_failed";
    failure.id = *queryId;
    failure.error_kind = "Disconnected";
    failure.error = "Connection closed";
    failure.vendor_code = "08006";
    adapter->eventReady(failure);
    choscordb::BridgeEvent cleanup{};
    cleanup.kind = "disconnected";
    cleanup.id = connection;
    adapter->eventReady(cleanup);
    auto* footer = window.findChild<QWidget*>("sqlResultFooter");
    QCOMPARE(footer->palette().color(QPalette::Window),
             choscordb::design::resolvedThemeForWidget(window).colors.dangerSurface);
    QVERIFY(footer->toolTip().contains("Failed: Connection closed [Code: 08006]"));
    QVERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                ->toPlainText()
                .contains("[Code: 08006]"));
    QVERIFY(window.findChild<QLabel*>("executionDuration")->text().isEmpty());
    QSignalSpy recovered(workspace, &choscordb::QueryWorkspace::connectionReady);
    workspace->connectSqlite(":memory:");
    QTRY_COMPARE(recovered.count(), 1);
    QCOMPARE(footer->palette().color(QPalette::Window),
             choscordb::design::resolvedThemeForWidget(window).colors.muted);
}

void ModernUiTest::embeddedDataFailureRemainsVisibleAfterDisconnectCleanup_data() {
    QTest::addColumn<bool>("activeExport");
    QTest::newRow("idle") << false;
    QTest::newRow("export-submission") << true;
}

void ModernUiTest::embeddedDataFailureRemainsVisibleAfterDisconnectCleanup() {
    QFETCH(bool, activeExport);
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* adapter = workspace->adapter();
    workspace->connectSqlite(":memory:");
    auto* run = window.findChild<QAction*>("runStatement");
    QTRY_VERIFY(run->isEnabled());
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    editor->setText("CREATE TABLE records(id INTEGER)");
    run->trigger();
    QTRY_COMPARE(window.findChild<QLabel*>("executionSummary")->property("state").toString(),
                 QString("completed"));
    const auto connection =
        window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
    std::optional<quint64> queryId;
    connect(adapter, &choscordb::EngineAdapter::eventReady, &window,
            [&queryId](const choscordb::BridgeEvent& event) {
                if (event.kind == "schema")
                    queryId = event.id;
            });
    window.objectContextSelected(connection, R"(["main","records"])", "records", "table");
    auto* explorer = qobject_cast<choscordb::ObjectExplorer*>(tabs->currentWidget());
    QVERIFY(explorer);
    auto* summary = explorer->findChild<QLabel*>("objectDataSummary");
    QTRY_COMPARE(summary->property("state").toString(), QString("completed"));
    QTRY_VERIFY(queryId.has_value());
    QTRY_VERIFY(explorer->findChild<QLabel*>("objectDataDuration")
                    ->accessibleName()
                    .startsWith("Executed in "));
    auto* footer = explorer->findChild<QWidget*>("objectDataFooter");
    const auto failureThenCleanup = [adapter, query = *queryId, connection] {
        choscordb::BridgeEvent failure{};
        failure.kind = "query_failed";
        failure.id = query;
        failure.error_kind = "Disconnected";
        failure.error = "Data connection closed";
        adapter->eventReady(failure);
        choscordb::BridgeEvent cleanup{};
        cleanup.kind = "disconnected";
        cleanup.id = connection;
        adapter->eventReady(cleanup);
    };
    if (activeExport) {
        auto* exportButton = explorer->findChild<QPushButton*>("objectDataExport");
        QTRY_VERIFY(exportButton->isEnabled());
        exportButton->click();
        auto* dialog = window.findChild<choscordb::ExportDialog*>();
        QVERIFY(dialog);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        connect(dialog, &choscordb::ExportDialog::exportRunningChanged, &window,
                [failureThenCleanup](bool running) {
                    if (running)
                        failureThenCleanup();
                });
        dialog->startExportTo(directory.filePath("data.csv"), "csv");
        QTRY_VERIFY(!dialog->isRunning());
    } else {
        failureThenCleanup();
    }
    QVERIFY(footer->isVisible());
    QVERIFY(!explorer->findChild<QWidget*>("objectFooter")->isVisible());
    QCOMPARE(footer->palette().color(QPalette::Window),
             choscordb::design::resolvedThemeForWidget(window).colors.dangerSurface);
    QVERIFY(footer->toolTip().contains("Failed: Data connection closed"));
    QVERIFY(!explorer->findChild<QPushButton*>("objectDataNext")->isEnabled());
    QVERIFY(explorer->findChild<QAction*>("objectReconnect")->isEnabled());
}
