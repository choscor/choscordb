#include "design_system/table/table_style.h"
#include "models/result_table_model.h"
#include <QSignalSpy>
#include <QtTest>
using namespace choscordb;
namespace {
ResultColumn column(const QString& name, const QString& databaseType) {
    ResultColumn value{};
    value.name = name;
    value.databaseType = databaseType;
    return value;
}
} // namespace
// How a page and its later metadata reach the views.
class ResultPageModelTest : public QObject {
    Q_OBJECT
  private slots:
    void unchangedEditabilityAndMetadataDoNotRepaintThePage() {
        ResultTableModel model;
        ResultCellMetadata metadata;
        metadata.nullable = true;
        metadata.enumChoices = {"ready", "queued"};
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        // Metadata that arrives with the page is part of the same reset.
        QVERIFY(model.setPage({column("state", "text")}, {{QString("ready")}, {QString("queued")}},
                              0, {metadata}));
        QCOMPARE(reset.count(), 1);
        QCOMPARE(changed.count(), 0);
        QVERIFY(model.setCellMetadata({metadata}));
        model.setEditableColumns({false}, false, false);
        QCOMPARE(changed.count(), 0);
        model.setEditableColumns({true}, false, false);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(model.index(1, 0).data(design::ChoiceLabelsRole).toStringList(),
                 (QStringList{"ready", "queued"}));
        model.setEditableColumns({true}, false, false);
        metadata.nullable = false;
        QVERIFY(model.setCellMetadata({metadata}));
        QCOMPARE(changed.count(), 2);
        QVERIFY(!model.index(0, 0).data(design::ChoiceNullableRole).toBool());
    }
    void multiDataGivesEachRoleTheSameValueAsData() {
        ResultTableModel model;
        QVERIFY(model.setPage(ResultTableModel::preparePage(
            {column("id", "integer"), column("note", "text"), column("payload", "blob")},
            {{qint64(1), QString("line\nbreak"), QByteArray::fromHex("00ff")},
             {std::monostate{}, QString("plain"), DeferredValue{7, 90000, "blob", false}},
             {qint64(3), QString(600, QChar('x')),
              FallbackText{QString("fallback"), QString("geometry")}}},
            {true, false, false, false, false, false, true, false, false}, 0)));
        ResultCellMetadata link;
        link.sourceColumn = "id";
        link.sourceObject = R"(["main","child"])";
        link.targetObject = R"(["main","parent"])";
        link.targetQualifiedName = "parent";
        link.targetColumn = "id";
        link.nullable = true;
        ResultCellMetadata choices;
        choices.enumChoices = {"plain", "other"};
        QVERIFY(model.setCellMetadata({link, choices, {}}));
        model.setEditableColumns({true, true, false}, true, true);
        QVERIFY(model.setData(model.index(1, 1), QString("other")));
        QVERIFY(model.addRow());
        QVERIFY(model.setData(model.index(3, 0), QString("5")));
        model.markRowsDeleted({2}, true);

        QList<int> roles;
        for (int role = Qt::DisplayRole; role <= design::CellChangeRole; ++role)
            roles.push_back(role);
        std::vector<QModelRoleData> requested(roles.begin(), roles.end());
        int distinct = 0;
        for (int row = 0; row < model.rowCount(); ++row)
            for (int column = 0; column < model.columnCount(); ++column) {
                const auto index = model.index(row, column);
                model.multiData(index, requested);
                for (const auto& item : requested) {
                    const auto expected = index.data(item.role());
                    QVERIFY2(item.data() == expected,
                             qPrintable(QString("row %1, column %2, role %3")
                                            .arg(row)
                                            .arg(column)
                                            .arg(item.role())));
                    distinct += expected.isValid();
                }
            }
        // The sample sets many roles, so equal empty values cannot pass the check.
        QVERIFY(distinct > model.rowCount() * model.columnCount() * 4);
    }
};
QTEST_MAIN(ResultPageModelTest)
#include "result_page_model_test.moc"
