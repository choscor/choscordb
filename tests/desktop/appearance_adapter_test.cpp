#include "bridge/engine_adapter.h"
#include <QMetaMethod>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class AppearanceAdapterTest final : public QObject {
    Q_OBJECT

  private slots:
    void themeChoicesMatchPersistedPolicy() {
        for (const auto& theme :
             {QStringLiteral("system"), QStringLiteral("light"), QStringLiteral("dark")})
            QVERIFY(choscordb::EngineAdapter::appearanceThemeValid(theme));
        for (const auto& theme : {QStringLiteral("sepia"), QStringLiteral("Dark"), QString()})
            QVERIFY(!choscordb::EngineAdapter::appearanceThemeValid(theme));
    }
    void newSettingsStartFromThePersistedDefaults() {
        const choscordb::AppearanceLayout layout;
        QCOMPARE(layout.navigatorWidth, 260u);
        QCOMPARE(layout.editorResultsSplit, quint16(500));
        QCOMPARE(layout.historyHeight, 220u);
        const choscordb::HistoryPolicy history;
        QVERIFY(history.enabled);
        QCOMPARE(history.maxAgeDays, 90u);
        QCOMPARE(history.maxRecords, 10000u);
        const choscordb::EditorPreferences editor;
        QCOMPARE(editor.fontSize,
                 choscordb::EngineAdapter::editorPreferenceLimits().defaultFontSize);
        QVERIFY(editor.shortcuts.isEmpty());
    }
    void exposesCorrelatedAppearanceResult() {
        choscordb::EngineAdapter adapter;
        QVERIFY(adapter.metaObject()->indexOfSignal(
                    "appearanceLayoutReady(quint64,bool,choscordb::AppearanceLayout)") >= 0);
    }

    void persistsAndResetsAValidatedRecord() {
        qRegisterMetaType<choscordb::AppearanceLayout>();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("appearance.sqlite");
        {
            choscordb::EngineAdapter adapter(nullptr, path);
            QSignalSpy ready(&adapter, &choscordb::EngineAdapter::appearanceLayoutReady);
            QVERIFY(adapter.getAppearanceLayout(41));
            QTRY_COMPARE(ready.count(), 1);
            QCOMPARE(ready.takeFirst().at(1).toBool(), false);

            choscordb::AppearanceLayout value;
            value.theme = "dark";
            value.navigatorWidth = 312;
            value.editorResultsSplit = 540;
            value.historyHeight = 240;
            value.historyVisible = true;
            value.x = 80;
            value.y = 60;
            value.width = 1200;
            value.height = 800;
            value.hasScreenName = true;
            value.screenName = "test-screen";
            QVERIFY(adapter.setAppearanceLayout(value, 42));
            QTRY_COMPARE(ready.count(), 1);
            const auto saved = ready.takeFirst();
            QCOMPARE(saved.at(0).toULongLong(), quint64(42));
            QCOMPARE(saved.at(1).toBool(), true);
        }
        {
            choscordb::EngineAdapter adapter(nullptr, path);
            QSignalSpy ready(&adapter, &choscordb::EngineAdapter::appearanceLayoutReady);
            QVERIFY(adapter.getAppearanceLayout(43));
            QTRY_COMPARE(ready.count(), 1);
            const auto loaded = qvariant_cast<choscordb::AppearanceLayout>(ready.at(0).at(2));
            QCOMPARE(loaded.theme, QString("dark"));
            QCOMPARE(loaded.navigatorWidth, quint32(312));
            QCOMPARE(loaded.screenName, QString("test-screen"));
            QVERIFY(adapter.resetAppearanceLayout(44));
            QTRY_COMPARE(ready.count(), 2);
            QCOMPARE(ready.at(1).at(1).toBool(), false);
        }
    }

    void rejectsOversizedScreenNameAtSubmission() {
        choscordb::EngineAdapter adapter;
        QSignalSpy failed(&adapter, &choscordb::EngineAdapter::recoveryFailed);
        choscordb::AppearanceLayout value;
        value.hasScreenName = true;
        value.screenName = QString(300, 'x');
        QVERIFY(!adapter.setAppearanceLayout(value, 99));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).toULongLong(), quint64(99));
    }

    void invalidAppearanceIsRejectedAtSubmissionWithoutReplacingSavedLayout() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        choscordb::EngineAdapter adapter(nullptr, directory.filePath("appearance.sqlite"));
        QSignalSpy ready(&adapter, &choscordb::EngineAdapter::appearanceLayoutReady);
        QSignalSpy failed(&adapter, &choscordb::EngineAdapter::recoveryFailed);
        choscordb::AppearanceLayout valid;
        valid.theme = "dark";
        QVERIFY(adapter.setAppearanceLayout(valid, 201));
        QTRY_COMPARE(ready.size(), 1);
        ready.clear();

        auto invalid = valid;
        invalid.theme = "sepia";
        QVERIFY(!adapter.setAppearanceLayout(invalid, 202));
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed.at(0).at(0).toULongLong(), quint64(202));
        QVERIFY(!failed.at(0).at(1).toString().isEmpty());

        invalid = valid;
        invalid.editorResultsSplit = 1001;
        QVERIFY(!adapter.setAppearanceLayout(invalid, 203));
        QTRY_COMPARE(failed.size(), 2);

        invalid = valid;
        invalid.width = 100;
        QVERIFY(!adapter.setAppearanceLayout(invalid, 204));
        QTRY_COMPARE(failed.size(), 3);

        QVERIFY(adapter.getAppearanceLayout(205));
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready.at(0).at(0).toULongLong(), quint64(205));
        QCOMPARE(qvariant_cast<choscordb::AppearanceLayout>(ready.at(0).at(2)).theme,
                 QString("dark"));
    }
};

QTEST_MAIN(AppearanceAdapterTest)
#include "appearance_adapter_test.moc"
