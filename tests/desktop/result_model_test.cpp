#include "bridge/result_column_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "models/result_table_model.h"
#include <QAbstractItemModelTester>
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
class ResultModelTest : public QObject {
    Q_OBJECT
  private slots:
    void unchangedEditorValuesDoNotStageEdits() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("text", "text"), column("number", "bigint"),
                               column("flag", "boolean"), column("null", "text")},
                              {{QString("hello"), qint64(42), true, std::monostate{}}}, 0));
        model.setEditableColumns({true, true, true, true}, true, true);
        for (int c = 0; c < model.columnCount(); ++c)
            QVERIFY(model.setData(model.index(0, c), model.data(model.index(0, c), Qt::EditRole)));
        QVERIFY(!model.hasPendingEdits());
        QVERIFY(std::holds_alternative<std::monostate>(model.rows()[0][3]));
        QVERIFY(model.setData(model.index(0, 0), "changed"));
        QVERIFY(model.hasPendingEdits());
        QVERIFY(model.setData(model.index(0, 0), "changed"));
        QVERIFY(model.touched()[0][0]);
        QVERIFY(model.addRow());
        QVERIFY(model.setData(model.index(1, 0), ""));
        QVERIFY(!model.touched()[1][0]);
        QVERIFY(model.setNull(model.index(1, 0)));
        QVERIFY(model.touched()[1][0]);
    }
    void deletingInsertedRowsRemovesThemAndRestoresBudget() {
        ResultTableModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVERIFY(model.setPage({column("a", "text"), column("b", "text")},
                              {{QString("original"), QString("kept")}}, 0));
        model.setEditableColumns({true, true}, true, true);
        for (int row = 1; row <= 3; ++row) {
            QVERIFY(model.addRow());
            QVERIFY(model.setData(model.index(row, 0), QString::number(row)));
        }
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        model.markDeleted(
            {model.index(1, 0), model.index(3, 0), model.index(1, 1), model.index(0, 0)}, true);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(removed.count(), 2);
        QCOMPARE(model.index(1, 0).data().toString(), QString("2"));
        QVERIFY(model.deleted()[0]);
        QVERIFY(!model.deleted()[1]);
        model.markDeleted({model.index(0, 0), model.index(1, 0)}, false);
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(!model.deleted()[0]);
        model.markDeleted({model.index(1, 0)}, true);
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(!model.hasPendingEdits());
        QVERIFY(model.setByteBudget(model.residentBytes()));
    }
    void insertedRowsCanBeRemovedWithoutDatabaseDeletePermission() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("value", "text")}, {{QString("original")}}, 0));
        model.setEditableColumns({false}, true, false);
        QVERIFY(model.addRow());
        model.markDeleted({model.index(0, 0), model.index(1, 0)}, true);
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(!model.deleted()[0]);
        QVERIFY(!model.hasPendingEdits());
    }
    void duplicateRowCopiesCurrentTypedInsertableValues() {
        ResultTableModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QByteArray binary("\0typed", 6);
        QVERIFY(model.setPage({column("id", "integer"), column("name", "text"),
                               column("missing", "text"), column("empty", "text"),
                               column("count", "integer"), column("enabled", "boolean"),
                               column("payload", "blob")},
                              {{qint64(7), QString("before"), std::monostate{}, QString(""),
                                qint64(23), true, binary}},
                              0));
        model.setEditableColumns({false, true, true, true, true, true, false}, true, true,
                                 {true, true, true, true, true, true, true});
        QVERIFY(model.setData(model.index(0, 1), QString("after")));

        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QString error;
        QVERIFY(model.duplicateRow(0, {false, true, true, true, true, true, true}, &error));
        QVERIFY(error.isEmpty());
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(model.inserted()[1]);
        QVERIFY(!model.touched()[1][0]);
        for (int column = 1; column < model.columnCount(); ++column)
            QVERIFY(model.touched()[1][column]);
        QVERIFY(std::holds_alternative<std::monostate>(model.rows()[1][0]));
        QCOMPARE(std::get<QString>(model.rows()[1][1]), QString("after"));
        QVERIFY(std::holds_alternative<std::monostate>(model.rows()[1][2]));
        QCOMPARE(std::get<QString>(model.rows()[1][3]), QString(""));
        QCOMPARE(std::get<qint64>(model.rows()[1][4]), qint64(23));
        QCOMPARE(std::get<bool>(model.rows()[1][5]), true);
        QCOMPARE(std::get<QByteArray>(model.rows()[1][6]), binary);
    }
    void duplicateRowRejectsDeferredValuesWithoutPartialMutation() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("id", "integer"), column("payload", "blob")},
                              {{qint64(4), DeferredValue{9, 500000, "blob"}}}, 0));
        model.setEditableColumns({false, false}, true, true, {false, true});
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy pending(&model, &ResultTableModel::pendingEditsChanged);
        QString error;

        QVERIFY(!model.duplicateRow(0, &error));
        QVERIFY(error.contains("load", Qt::CaseInsensitive));
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(pending.count(), 0);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(std::get<DeferredValue>(model.rows()[0][1]).handle, quint64(9));
        QVERIFY(!model.hasPendingEdits());
    }
    void duplicateRowRejectsBudgetOverflowWithoutPartialMutation() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("value", "text")}, {{QString("kept")}}, 0));
        model.setEditableColumns({true}, true, true, {true});
        QVERIFY(model.setByteBudget(model.residentBytes()));
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QString error;

        QVERIFY(!model.duplicateRow(0, &error));
        QVERIFY(error.contains("memory", Qt::CaseInsensitive));
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0, 0)).toString(), QString("kept"));
        QVERIFY(!model.hasPendingEdits());
    }
    void nullAndEmptyAreDistinct() {
        ResultTableModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVERIFY(
            model.setPage({column("value", "text")}, {{std::monostate{}}, {QString("")}}, 1000));
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0, 0)).toString(), QString("NULL"));
        QCOMPARE(model.data(model.index(1, 0)).toString(), QString(""));
        QCOMPARE(model.headerData(0, Qt::Vertical).toULongLong(), quint64(1001));
        QVERIFY(model.setPage({column("value", "text")}, {{QString("next")}}, 2000));
        QCOMPARE(model.rowCount(), 1);
    }
    void columnMetadataIsPreservedAndExposed() {
        ResultTableModel model;
        QVERIFY(
            model.setPage({{"amount", "numeric", 30u, -2, "UTC", false},
                           {"optional", "text", std::nullopt, std::nullopt, {}, true},
                           {"expression", "integer", std::nullopt, std::nullopt, {}, std::nullopt}},
                          {}, 0));

        QCOMPARE(model.headerData(0, Qt::Horizontal).toString(),
                 QString("amount · numeric(30,-2)"));
        QCOMPARE(
            model.headerData(0, Qt::Horizontal, Qt::ToolTipRole).toString(),
            QString("Name: amount\nDatabase type: numeric\nPrecision: 30\nScale: -2\nTimezone: "
                    "UTC\nNullability: Not nullable"));
        QCOMPARE(model.headerData(1, Qt::Horizontal, Qt::AccessibleDescriptionRole).toString(),
                 QString("Name: optional\nDatabase type: text\nPrecision: Unknown\nScale: "
                         "Unknown\nTimezone: Unknown\nNullability: Nullable"));
        QVERIFY(model.headerData(0, Qt::Vertical, Qt::ToolTipRole).isNull());
        QCOMPARE(model.headerData(2, Qt::Horizontal, Qt::ToolTipRole).toString(),
                 QString("Name: expression\nDatabase type: integer\nPrecision: Unknown\nScale: "
                         "Unknown\nTimezone: Unknown\nNullability: Unknown"));
    }
    void bridgeColumnPreservesEverySchemaField() {
        choscordb::ColumnDto dto;
        dto.name = "stamp";
        dto.database_type = "timestamptz";
        dto.has_precision = true;
        dto.precision = 3;
        dto.has_scale = true;
        dto.scale = -2;
        dto.timezone = "UTC";
        dto.nullability = 0;

        ResultTableModel model;
        QVERIFY(model.setPage({resultColumn(dto)}, {}, 0));
        QCOMPARE(model.headerData(0, Qt::Horizontal).toString(),
                 QString("stamp · timestamptz(3,-2)"));
        QVERIFY(model.headerData(0, Qt::Horizontal, Qt::ToolTipRole)
                    .toString()
                    .endsWith("Timezone: UTC\nNullability: Not nullable"));

        dto.has_precision = false;
        dto.has_scale = false;
        dto.nullability = -1;
        QVERIFY(model.setPage({resultColumn(dto)}, {}, 0));
        QVERIFY(model.headerData(0, Qt::Horizontal, Qt::ToolTipRole)
                    .toString()
                    .endsWith("Timezone: UTC\nNullability: Unknown"));
    }
    void binaryCopyIsCompleteAndSparseCopyPreservesCoordinates() {
        ResultTableModel model;
        const QByteArray binary(100, '\xab');
        QVERIFY(model.setPage({column("a", "blob"), column("b", "text"), column("c", "text")},
                              {{binary, QString("b"), QString("c")},
                               {QString("d"), QString("e"), QString("f")},
                               {QString("g"), QString("h"), QString("i")}},
                              0));
        QCOMPARE(model.copyCells({model.index(0, 0)}),
                 QString("0x") + QString::fromLatin1(binary.toHex()));
        QCOMPARE(model.copyCells({model.index(0, 1), model.index(2, 2)}), QString("b\t\n\t\n\ti"));
    }
    void deferredCopyReportsAnError() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("blob", "blob")}, {{DeferredValue{1, 500000, "blob"}}}, 0));
        QString error;
        QVERIFY(model.copyCells({model.index(0, 0)}, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void allocationBudgetIncludesCapacityAndRejectsAtomically() {
        ResultTableModel model(nullptr, 4096);
        QVERIFY(model.setPage({column("a", "text")}, {{QString("kept")}}, 0));
        const auto previous = model.residentBytes();
        QVERIFY(previous >=
                sizeof(ResultColumn) + sizeof(ResultTableModel::Row) + sizeof(Cell) + 10);
        QVERIFY(!model.setByteBudget(previous - 1));
        QCOMPARE(model.byteBudget(), std::size_t(4096));
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QString reserved("tiny");
        reserved.reserve(10000);
        std::vector<ResultTableModel::Row> rows;
        rows.push_back({std::move(reserved)});
        QVERIFY(!model.setPage({column("a", "text")}, std::move(rows), 0));
        QCOMPARE(reset.count(), 0);
        QCOMPARE(model.residentBytes(), previous);
        QCOMPARE(model.data(model.index(0, 0)).toString(), QString("kept"));
        std::vector<ResultTableModel::Row> spareRows;
        spareRows.reserve(1000);
        QVERIFY(!model.setPage({}, std::move(spareRows), 0));
        QVERIFY(model.setPage({}, {}, 0));
        QCOMPARE(model.residentBytes(), std::size_t(0));
        QVERIFY(model.setByteBudget(0));
    }
    void binaryAndDeferredTypeCapacitiesCount() {
        ResultTableModel model(nullptr, 4096);
        QByteArray blob("a");
        blob.reserve(10000);
        std::vector<ResultTableModel::Row> rows;
        rows.push_back({std::move(blob)});
        QVERIFY(!model.setPage({column("a", "blob")}, std::move(rows), 0));
        QString type("blob");
        type.reserve(10000);
        rows.push_back({DeferredValue{1, 500000, std::move(type)}});
        QVERIFY(!model.setPage({column("a", "blob")}, std::move(rows), 0));
        QVERIFY(model.setPage({column("a", "blob")}, {{DeferredValue{1, 500000, "blob"}}}, 0));
        QVERIFY(model.residentBytes() < 4096);
    }
    void copyingPreservesExactDecimalsAndEscapesTsv() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("amount", "numeric"), column("note", "text")},
                              {{QString("12345678901234567890.001"), QString("a\tb\n\"c\"")}}, 0));
        QCOMPARE(model.copyCells({model.index(0, 1), model.index(0, 0)}),
                 QString("12345678901234567890.001\t\"a\tb\n\"\"c\"\"\""));
        QVERIFY(!model.setPage({column("one", "text")}, {{QString("a"), QString("b")}}, 0));
        QCOMPARE(model.columnCount(), 2);
    }
};
QTEST_MAIN(ResultModelTest)
#include "result_model_test.moc"
