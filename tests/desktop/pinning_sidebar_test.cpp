#include "app/main_window.h"
#include "app/pin_store.h"
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>

class PinningSidebarTest final : public QObject {
    Q_OBJECT

  private slots:
    void pinnedSectionShowsEmptyStateWithoutSelection() {
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
        window.show();

        auto* panel = window.findChild<QWidget*>("connectionsPanel");
        auto* section = window.findChild<QWidget*>("pinnedSection");
        auto* list = window.findChild<QListWidget*>("pinnedList");
        auto* empty = window.findChild<QLabel*>("pinnedEmpty");
        auto* filter = window.findChild<QLineEdit*>("navigatorFilter");
        QVERIFY(panel && section && list && empty && filter);
        auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
        QVERIFY(layout);
        QCOMPARE(layout->itemAt(1)->widget(), section);
        QCOMPARE(layout->itemAt(2)->widget()->findChild<QLineEdit*>("navigatorFilter"), filter);
        QVERIFY(section->isVisible());
        QVERIFY(!section->accessibleName().isEmpty());
        QTRY_VERIFY(empty->isVisible());
        QVERIFY(empty->text().contains("pin", Qt::CaseInsensitive));
        QCOMPARE(list->count(), 0);
        QVERIFY(!list->isVisible());
    }

    void restoredPinsScrollAndIgnoreObjectFilter() {
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        const auto path = storage.filePath("settings.sqlite");
        QList<choscordb::PinRecord> pins;
        for (int index = 0; index < 31; ++index) {
            const auto name = QStringLiteral("object_%1").arg(index);
            pins.append(choscordb::PinRecord{
                .profileId = QStringLiteral("saved-profile"),
                .profileName = QStringLiteral("Saved profile"),
                .objectId = QStringLiteral("pg:relation:%1").arg(index),
                .name = name,
                .qualifiedName = QStringLiteral("public.%1").arg(name),
                .kind = QStringLiteral("table"),
                .parentObjectId = QStringLiteral("pg:schema:1"),
                .ancestryIds = {QStringLiteral("pg:database:1"), QStringLiteral("pg:schema:1")},
                .ancestryNames = {QStringLiteral("database"), QStringLiteral("public")}});
        }
        QString error;
        QVERIFY2(choscordb::PinStore(path).save(pins, &error), qPrintable(error));

        choscordb::MainWindow window(nullptr, path);
        window.show();
        auto* list = window.findChild<QListWidget*>("pinnedList");
        auto* empty = window.findChild<QLabel*>("pinnedEmpty");
        auto* filter = window.findChild<QLineEdit*>("navigatorFilter");
        QVERIFY(list && empty && filter);
        QTRY_COMPARE(list->count(), 31);
        QVERIFY(list->isVisible());
        QVERIFY(!empty->isVisible());
        QVERIFY(!list->accessibleName().isEmpty());
        QVERIFY(list->maximumHeight() < window.height() / 2);
        QTRY_VERIFY(list->verticalScrollBar()->maximum() > 0);
        QVERIFY(list->item(0)->text().contains("object_0"));
        QVERIFY(list->item(0)->text().contains("Saved profile"));

        filter->setText("different_object");
        QTRY_COMPARE(list->count(), 31);
        QVERIFY(list->isVisible());
        QVERIFY(list->item(0)->text().contains("object_0"));
    }
};

QTEST_MAIN(PinningSidebarTest)
#include "pinning_sidebar_test.moc"
