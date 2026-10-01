#include "bridge/engine_adapter.h"
#include "bridge/result_column_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/table/table_style.h"
#include "models/result_table_model.h"
#include <QAbstractItemModelTester>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <future>
#include <limits>
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
    void explicitTextDraftDistinguishesNullEmptyAndOmitted() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("payload", "text"), column("other", "text")},
                              {{std::monostate{}, QString("kept")}}, 0));
        model.setEditableColumns({true, true}, true, false);
        QVERIFY(model.setData(model.index(0, 1), "staged elsewhere"));
        const auto cell = model.index(0, 0);
        auto task =
            std::async(std::launch::async, [snapshot = model.cellEditSnapshot(cell, "NULL")] {
                return ResultTableModel::evaluateCellEdit(snapshot);
            });
        const auto result = model.stageCellEdit(cell, task.get());
        QCOMPARE(result.state, ResultTableModel::CellEditState::Ready);
        QCOMPARE(std::get<QString>(*model.cellValue(cell)), QString("NULL"));
        QCOMPARE(std::get<QString>(model.rows()[0][1]), QString("staged elsewhere"));
        QVERIFY(model.addRow());
        QVERIFY(!model.touched()[1][0]);
        const auto inserted = model.index(1, 0);
        const auto empty = model.stageCellEdit(
            inserted, ResultTableModel::evaluateCellEdit(model.cellEditSnapshot(inserted, "")));
        QCOMPARE(empty.state, ResultTableModel::CellEditState::Ready);
        QVERIFY(model.touched()[1][0]);
        QCOMPARE(std::get<QString>(*model.cellValue(inserted)), QString(""));
    }
    void explicitDraftRefusalLeavesTargetAndOtherEditsIntact() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("count", "integer"), column("other", "text")},
                              {{qint64(7), QString("original")}}, 0));
        model.setEditableColumns({true, true}, false, true);
        QVERIFY(model.setData(model.index(0, 1), "unrelated staged value"));
        const auto target = model.index(0, 0);
        const auto invalid =
            ResultTableModel::evaluateCellEdit(model.cellEditSnapshot(target, "oops"));
        QCOMPARE(invalid.state, ResultTableModel::CellEditState::TypeRejected);
        QVERIFY(invalid.error.contains("whole number"));
        QCOMPARE(model.stageCellEdit(target, invalid).state,
                 ResultTableModel::CellEditState::TypeRejected);
        QCOMPARE(std::get<qint64>(*model.cellValue(target)), qint64(7));
        QVERIFY(!model.touched()[0][0]);
        // The budget allows the current state, but cannot hold a new target draft.
        QVERIFY(model.setByteBudget(model.residentBytes() + 256));
        const auto oversized = ResultTableModel::evaluateCellEdit(
            model.cellEditSnapshot(target, QString(1024, QChar('8'))));
        QCOMPARE(oversized.state, ResultTableModel::CellEditState::ResourceRefused);
        QVERIFY(!oversized.error.isEmpty());
        QCOMPARE(model.stageCellEdit(target, oversized).state,
                 ResultTableModel::CellEditState::ResourceRefused);
        QCOMPARE(std::get<qint64>(*model.cellValue(target)), qint64(7));
        QCOMPARE(std::get<QString>(model.rows()[0][1]), QString("unrelated staged value"));
        const auto corrected = model.stageCellEdit(
            target, ResultTableModel::evaluateCellEdit(model.cellEditSnapshot(target, "9")));
        QCOMPARE(corrected.state, ResultTableModel::CellEditState::Ready);
        QCOMPARE(std::get<qint64>(*model.cellValue(target)), qint64(9));
    }
    void typedStagingRechecksEligibilityAndCurrentBudget() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("payload", "text")}, {{QString("original")}}, 0));
        model.setEditableColumns({true}, false, true);
        const auto target = model.index(0, 0);
        const auto evaluation =
            ResultTableModel::evaluateCellEdit(model.cellEditSnapshot(target, "changed"));
        QCOMPARE(evaluation.state, ResultTableModel::CellEditState::Ready);
        QVERIFY(model.setByteBudget(model.residentBytes()));
        const auto refused = model.stageCellEdit(target, evaluation);
        QCOMPARE(refused.state, ResultTableModel::CellEditState::ResourceRefused);
        QVERIFY(!refused.error.isEmpty());
        QCOMPARE(std::get<QString>(*model.cellValue(target)), QString("original"));
        QVERIFY(!model.hasPendingEdits());
        QVERIFY(model.setByteBudget(ResultTableModel::DefaultBytes));
        model.markDeleted({target}, true);
        const auto ineligible = model.stageCellEdit(target, evaluation);
        QCOMPARE(ineligible.state, ResultTableModel::CellEditState::Ineligible);
        QVERIFY(!ineligible.error.isEmpty());
        QVERIFY(!model.touched()[0][0]);
        QCOMPARE(std::get<QString>(*model.cellValue(target)), QString("original"));
        ResultTableModel other;
        QVERIFY(other.setPage({column("payload", "text")}, {{QString("foreign")}}, 0));
        other.setEditableColumns({true}, false, false);
        QCOMPARE(
            ResultTableModel::evaluateCellEdit(model.cellEditSnapshot(other.index(0, 0), "bad"))
                .state,
            ResultTableModel::CellEditState::Ineligible);
    }
    void jsonSnapshotCanRenderAfterModelChangesOnWorker() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("payload", "json")}, {{QString("{\"old\":1}")}}, 0));
        auto snapshot = model.jsonViewSnapshot(ResultTableModel::JsonViewScope::Row, 0, 0);
        QVERIFY(snapshot.has_value());
        QVERIFY(model.setPage({column("payload", "json")}, {{QString("{\"new\":2}")}}, 0));
        auto task = std::async(std::launch::async, [value = std::move(*snapshot)]() mutable {
            return ResultTableModel::evaluateJsonView(std::move(value));
        });
        const auto rendered = task.get();
        QCOMPARE(rendered.state, ResultTableModel::JsonViewState::Ready);
        QCOMPARE(rendered.json, QString("{\n  \"payload\": {\n    \"old\": 1\n  }\n}"));
    }
    void gridEditsParseDatabaseTypesThroughBridge() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("count", "INTEGER"), column("flag", "bool"),
                               column("amount", "NUMERIC(30,2)"), column("ratio", "float8"),
                               column("custom", "custom_type")},
                              {{qint64(0), false, DecimalValue{"0"}, 0.0, QString("old")}}, 0));
        model.setEditableColumns({true, true, true, true, true}, false, false);
        QVERIFY(model.setData(model.index(0, 0), QString(" -9223372036854775808 ")));
        QCOMPARE(std::get<qint64>(*model.cellValue(model.index(0, 0))),
                 std::numeric_limits<qint64>::min());
        QVERIFY(!model.setData(model.index(0, 0), QString("9223372036854775808")));
        QVERIFY(model.setData(model.index(0, 1), QString("TrUe")));
        QVERIFY(std::get<bool>(*model.cellValue(model.index(0, 1))));
        QVERIFY(!model.setData(model.index(0, 1), QString(" true ")));
        QVERIFY(model.setData(model.index(0, 2), QString("2.500")));
        QCOMPARE(std::get<DecimalValue>(*model.cellValue(model.index(0, 2))).text,
                 QString("2.500"));
        QVERIFY(model.setData(model.index(0, 3), QString("1.25")));
        QCOMPARE(std::get<double>(*model.cellValue(model.index(0, 3))), 1.25);
        QVERIFY(!model.setData(model.index(0, 3), QString("1e309")));
        QVERIFY(model.setData(model.index(0, 4), QString("02.5")));
        QCOMPARE(std::get<QString>(*model.cellValue(model.index(0, 4))), QString("02.5"));
    }
    void foreignKeyBridgeQuotesAndRejectsUnsafeValues() {
        QCOMPARE(EngineAdapter::foreignKeyPredicate(QStringLiteral("key\"name"),
                                                    Cell{QStringLiteral("O'Reilly")}),
                 std::optional<QString>{QStringLiteral("\"key\"\"name\" = 'O''Reilly'")});
        QCOMPARE(EngineAdapter::foreignKeyPredicate(QStringLiteral("id"), Cell{qint64(-42)}),
                 std::optional<QString>{QStringLiteral("\"id\" = -42")});
        QVERIFY(EngineAdapter::foreignKeyValueFilterable(Cell{DecimalValue{"2.50"}}));
        QVERIFY(
            !EngineAdapter::foreignKeyValueFilterable(Cell{DecimalValue{"1.0000000000000001"}}));
        QVERIFY(!EngineAdapter::foreignKeyPredicate(QStringLiteral("id"),
                                                    Cell{DeferredValue{7, 50, "text"}}));
        QVERIFY(!EngineAdapter::foreignKeyPredicate(QString(), Cell{qint64(1)}));
    }
    void typedNullChoiceStagesNullRatherThanText() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("state", "text")}, {{QString("ready")}}, 0));
        model.setEditableColumns({true}, false, false);
        ResultCellMetadata metadata;
        metadata.nullable = true;
        metadata.enumChoices = {"ready", "queued"};
        QVERIFY(model.setCellMetadata({metadata}));
        const auto cell = model.index(0, 0);
        QCOMPARE(cell.data(design::ChoiceLabelsRole).toStringList(),
                 (QStringList{"ready", "queued"}));
        QVERIFY(cell.data(design::ChoiceNullableRole).toBool());
        QVERIFY(model.setData(cell, QVariant(), design::TypedNullEditRole));
        QVERIFY(model.hasPendingEdits());
        QVERIFY(std::holds_alternative<std::monostate>(model.rows()[0][0]));
        model.discardEdits();
        QCOMPARE(cell.data().toString(), QString("ready"));
    }
    void verifiedLinksFollowCurrentTypedValueAndHideUnavailableCells() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("parent_id", "integer")},
                              {{qint64(4)}, {std::monostate{}}, {DeferredValue{1, 100, "integer"}}},
                              0));
        ResultCellMetadata metadata;
        metadata.sourceColumn = "parent_id";
        metadata.sourceObject = R"(["main","child"])";
        metadata.targetObject = "[\"main\",\"parent\"]";
        metadata.targetQualifiedName = "parent";
        metadata.targetColumn = "id";
        QVERIFY(model.setCellMetadata({metadata}));
        QCOMPARE(model.index(0, 0).data(design::ForeignKeyLinkLabelRole).toString(),
                 QString("Open referenced row in parent (id)"));
        QVERIFY(!model.index(1, 0).data(design::ForeignKeyLinkLabelRole).isValid());
        QVERIFY(!model.index(2, 0).data(design::ForeignKeyLinkLabelRole).isValid());
        model.setEditableColumns({true}, false, false);
        QVERIFY(model.setData(model.index(0, 0), QString("9")));
        QCOMPARE(std::get<qint64>(*model.cellValue(model.index(0, 0))), qint64(9));
        QVERIFY(model.setNull(model.index(0, 0)));
        QVERIFY(!model.index(0, 0).data(design::ForeignKeyLinkLabelRole).isValid());
    }
    void rowJsonReadinessFindsInvalidCellsAfterDeferredValues() {
        ResultTableModel model;
        QString invalid;
        invalid.append(QChar(0xd800));
        QVERIFY(model.setPage({column("payload", "blob"), column("text", "text")},
                              {{DeferredValue{42, 70000, "binary"}, invalid}}, 0));
        QString error;
        QCOMPARE(model.rowJsonReadiness(0, &error), ResultTableModel::RowJsonReadiness::Invalid);
        QVERIFY(!error.isEmpty());
        QVERIFY(model.setPage({column("payload", "blob"), column("text", "text")},
                              {{DeferredValue{42, 70000, "binary"}, QString("valid")}}, 0));
        QCOMPARE(model.rowJsonReadiness(0, &error),
                 ResultTableModel::RowJsonReadiness::NeedsDeferred);
        QVERIFY(error.isEmpty());
        QVERIFY(model.setPage({column("text", "text")}, {{QString("valid")}}, 0));
        QCOMPARE(model.rowJsonReadiness(0, &error), ResultTableModel::RowJsonReadiness::Ready);
    }
    void rowJsonPreservesTypedValuesAndStagedState() {
        ResultTableModel model;
        QVERIFY(model.setPage(
            {column("null", "text"), column("empty", "text"), column("large", "bigint"),
             column("fraction", "double precision"), column("flag", "boolean"),
             column("json text", "jsonb"), column("plain text", "text"), column("bytes", "blob")},
            {{std::monostate{}, QString(""), qint64(9223372036854775807LL), 1.25, false,
              QString("{\"a\":1}"), QString("{\"a\":1}"), QByteArray("\0\xff", 2)}},
            0));
        model.setEditableColumns({true, true, true, true, true, true, true, false}, true, true);
        QVERIFY(model.setData(model.index(0, 4), "true"));
        QString json, error;
        QVERIFY(model.rowJson(0, &json, &error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(json, QString("{\n"
                               "  \"null\": null,\n"
                               "  \"empty\": \"\",\n"
                               "  \"large\": 9223372036854775807,\n"
                               "  \"fraction\": 1.25,\n"
                               "  \"flag\": true,\n"
                               "  \"json text\": {\n"
                               "    \"a\": 1\n"
                               "  },\n"
                               "  \"plain text\": \"{\\\"a\\\":1}\",\n"
                               "  \"bytes\": {\"$binary\": \"AP8=\"}\n"
                               "}"));
        const auto parsed = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(parsed.isObject());
        QCOMPARE(parsed.object().value("empty").toString(), QString(""));
        QVERIFY(parsed.object().value("null").isNull());
        QCOMPARE(parsed.object().value("json text").toObject().value("a").toInt(), 1);
        QVERIFY(parsed.object().value("plain text").isString());
    }
    void rowJsonKeepsAllCollidingAndUnnamedColumns() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("name", "text"), column("name", "text"),
                               column("name (2)", "text"), column("", "text"),
                               column("column 4", "text"), column("", "text")},
                              {{QString("one"), QString("two"), QString("three"), QString("four"),
                                QString("five"), QString("six")}},
                              0));
        QString json, error;
        QVERIFY(model.rowJson(0, &json, &error));
        const auto parsed = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(parsed.isObject());
        const auto object = parsed.object();
        QCOMPARE(object.size(), 6);
        QCOMPARE(object.value("name").toString(), QString("one"));
        QCOMPARE(object.value("name (3)").toString(), QString("two"));
        QCOMPARE(object.value("name (2)").toString(), QString("three"));
        QCOMPARE(object.value("column 4 (2)").toString(), QString("four"));
        QCOMPARE(object.value("column 4").toString(), QString("five"));
        QCOMPARE(object.value("column 6").toString(), QString("six"));
        QVERIFY(json.indexOf("\"name (3)\"") < json.indexOf("\"name (2)\""));
        QString again;
        QVERIFY(model.rowJson(0, &again));
        QCOMPARE(again, json);
    }
    void rowJsonMarksOmittedInsertFieldsWithoutConfusingNullAndEmpty() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("omitted", "text"), column("null", "text"),
                               column("empty", "text"), column("binary", "blob")},
                              {}, 0));
        model.setEditableColumns({true, true, true, true}, true, true);
        QVERIFY(model.addRow());
        QVERIFY(model.setNull(model.index(0, 1)));
        QVERIFY(model.setData(model.index(0, 2), "x"));
        QVERIFY(model.setData(model.index(0, 2), ""));
        QString json, error;
        QVERIFY(model.rowJson(0, &json, &error));
        const auto object = QJsonDocument::fromJson(json.toUtf8()).object();
        QCOMPARE(object.value("omitted").toObject().value("$omitted").toBool(), true);
        QVERIFY(object.value("null").isNull());
        QCOMPARE(object.value("empty").toString(), QString(""));
        QCOMPARE(object.value("binary").toObject().value("$omitted").toBool(), true);
    }
    void rowJsonRequiresCompleteDeferredValuesAndRejectsInvalidNumbers() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("text", "text"), column("bytes", "blob")},
                              {{DeferredValue{91, 4, "text"}, DeferredValue{92, 0, "blob"}}}, 0));
        QString json = "stale", error;
        QVERIFY(!model.rowJson(0, &json, &error));
        QVERIFY(json.isEmpty());
        QVERIFY(error.contains("deferred", Qt::CaseInsensitive));
        QVERIFY(!model.rowJson(0, &json, &error, {{0, QString("abc")}, {1, QByteArray()}}));
        QVERIFY(json.isEmpty());
        QVERIFY(error.contains("incomplete", Qt::CaseInsensitive));
        QVERIFY(model.rowJson(0, &json, &error, {{0, QString("full")}, {1, QByteArray()}}));
        QCOMPARE(QJsonDocument::fromJson(json.toUtf8()).object().value("text").toString(),
                 QString("full"));
        QCOMPARE(QJsonDocument::fromJson(json.toUtf8())
                     .object()
                     .value("bytes")
                     .toObject()
                     .value("$binary")
                     .toString(),
                 QString(""));
        QVERIFY(!model.rowJson(5, &json, &error));
        QVERIFY(json.isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(model.setPage({column("number", "real")},
                              {{std::numeric_limits<double>::infinity()}}, 0));
        QVERIFY(!model.rowJson(0, &json, &error));
        QVERIFY(json.isEmpty());
        QVERIFY(error.contains("non-finite", Qt::CaseInsensitive));
    }
    void rowJsonEscapesLongTextAtSpanBoundaries() {
        QString value(1024 * 1024, 'x');
        value[0] = '"';
        value[4096] = '\\';
        value[8192] = QChar(0x001f);
        value += QString::fromUcs4(U"\U0001f642");
        value += '\n';
        ResultTableModel model;
        QVERIFY(model.setPage({column("payload", "text")}, {{value}}, 0));
        QString json, error;
        QVERIFY(model.rowJson(0, &json, &error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(QJsonDocument::fromJson(json.toUtf8()).object().value("payload").toString(),
                 value);
        QVERIFY(json.contains(QStringLiteral("\\u001f")));
        QVERIFY(json.endsWith(QStringLiteral("\\u000a\"\n}")));
    }
    void cellJsonEligibilityUsesTypedCompleteContent() {
        ResultTableModel model;
        QVERIFY(model.setPage(
            {column("json", "JSONB"), column("scalar text", "text"),
             column("invalid text", "varchar(80)"), column("number", "integer"),
             column("binary", "blob"), column("sql null", "json"), column("deferred text", "text")},
            {{QString(R"({"n":9223372036854775807,"n":2})"), QString("true"), QString("{broken"),
              qint64(1), QByteArray("1"), std::monostate{}, DeferredValue{91, 4, "text"}}},
            0));
        QCOMPARE(model.cellJsonReadiness(model.index(0, 0)),
                 ResultTableModel::CellJsonReadiness::Ready);
        QCOMPARE(model.cellJsonReadiness(model.index(0, 1)),
                 ResultTableModel::CellJsonReadiness::Ready);
        for (int column : {2, 3, 4, 5})
            QCOMPARE(model.cellJsonReadiness(model.index(0, column)),
                     ResultTableModel::CellJsonReadiness::Unavailable);
        QCOMPARE(model.cellJsonReadiness(model.index(0, 6)),
                 ResultTableModel::CellJsonReadiness::NeedsDeferred);
        QString json, error;
        QVERIFY(model.cellJson(model.index(0, 0), &json, &error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(json.contains("9223372036854775807"));
        QVERIFY(json.indexOf("\"n\"") != json.lastIndexOf("\"n\""));
        QVERIFY(model.cellJson(model.index(0, 1), &json, &error));
        QCOMPARE(json, QString("true"));
        QVERIFY(!model.cellJson(model.index(0, 6), &json, &error));
        QVERIFY(json.isEmpty());
        QVERIFY(error.contains("deferred", Qt::CaseInsensitive));
        QVERIFY(model.cellJson(model.index(0, 6), &json, &error, Cell{QString("null")}));
        QCOMPARE(json, QString("null"));
    }
    void stagedMalformedJsonStaysInspectableButClearsOutput() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("document", "jsonb"), column("plain", "text")},
                              {{QString("{}"), QString("{}")}}, 0));
        model.setEditableColumns({true, true}, false, false);
        QVERIFY(model.setData(model.index(0, 0), "{bad"));
        QVERIFY(model.setData(model.index(0, 1), "{bad"));
        QString error, json = "stale";
        QCOMPARE(model.cellJsonReadiness(model.index(0, 0), &error),
                 ResultTableModel::CellJsonReadiness::Invalid);
        QVERIFY(!error.isEmpty());
        QCOMPARE(model.cellJsonReadiness(model.index(0, 1), &error),
                 ResultTableModel::CellJsonReadiness::Unavailable);
        QVERIFY(!model.cellJson(model.index(0, 0), &json, &error));
        QVERIFY(json.isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(!model.rowJson(0, &json, &error));
        QVERIFY(json.isEmpty());
    }
    void pageJsonIncludesLoadedRowsInGridOrderWithStagedValues() {
        ResultTableModel model;
        QVERIFY(model.setPage(
            {column("document", "jsonb"), column("plain", "text"), column("large", "bigint")},
            {{QString(R"({"row":1})"), QString(R"({"plain":true})"), qint64(9223372036854775807LL)},
             {QString("[false,null]"), QString("second"), qint64(-7)}},
            1000));
        model.setEditableColumns({true, true, true}, true, true);
        model.markDeleted({model.index(1, 0)}, true);
        QVERIFY(model.addRow());
        QVERIFY(model.setData(model.index(2, 0), "true"));
        QCOMPARE(model.pageJsonReadiness(), ResultTableModel::RowJsonReadiness::Ready);
        QString json, error;
        QVERIFY(model.pageJson(&json, &error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(json.contains("9223372036854775807"));
        const auto document = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(document.isArray());
        const auto rows = document.array();
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].toObject().value("document").toObject().value("row").toInt(), 1);
        QCOMPARE(rows[0].toObject().value("plain").toString(), QString(R"({"plain":true})"));
        QCOMPARE(rows[1].toObject().value("document").toArray().size(), 2);
        QCOMPARE(rows[1].toObject().value("large").toInt(), -7);
        QVERIFY(rows[2].toObject().value("document").toBool());
        QVERIFY(rows[2].toObject().value("plain").toObject().value("$omitted").toBool());
    }
    void pageJsonRequiresCompleteDeferredValuesAndRejectsInvalidRows() {
        ResultTableModel model;
        QVERIFY(model.setPage(
            {column("document", "json"), column("name", "text")},
            {{DeferredValue{8, 4, "text"}, QString("first")}, {QString("{}"), QString("second")}},
            0));
        QCOMPARE(model.pageJsonReadiness(), ResultTableModel::RowJsonReadiness::NeedsDeferred);
        QString json = "stale", error;
        QVERIFY(!model.pageJson(&json, &error));
        QVERIFY(json.isEmpty());
        QVERIFY(error.contains("deferred", Qt::CaseInsensitive));
        QVERIFY(!model.pageJson(&json, &error, {{{0, 0}, QByteArray("null")}}));
        QVERIFY(json.isEmpty());
        QVERIFY(error.contains("invalid", Qt::CaseInsensitive));
        QVERIFY(model.pageJson(&json, &error, {{{0, 0}, QString("null")}}));
        QCOMPARE(QJsonDocument::fromJson(json.toUtf8()).array().size(), 2);
        QVERIFY(QJsonDocument::fromJson(json.toUtf8())
                    .array()[0]
                    .toObject()
                    .value("document")
                    .isNull());

        QVERIFY(model.setPage({column("document", "json")},
                              {{DeferredValue{8, 4, "text"}}, {QString("{bad")}}, 0));
        QCOMPARE(model.pageJsonReadiness(&error), ResultTableModel::RowJsonReadiness::Invalid);
        QVERIFY(!error.isEmpty());
        QVERIFY(!model.pageJson(&json, &error));
        QVERIFY(json.isEmpty());
    }
    void pageJsonRejectsAggregateOutputOverBudgetWithoutPartialDocument() {
        ResultTableModel model;
        const QString escaping(50000, '\n');
        QVERIFY(model.setPage({column("value", "text")},
                              {{escaping},
                               {escaping},
                               {escaping},
                               {escaping},
                               {escaping},
                               {escaping},
                               {escaping},
                               {escaping}},
                              0));
        QVERIFY(model.setByteBudget(1048576));
        QString row, json = "stale", error;
        QVERIFY(model.rowJson(0, &row, &error));
        QVERIFY(!row.isEmpty());
        QVERIFY(!model.pageJson(&json, &error));
        QVERIFY(json.isEmpty());
        QVERIFY(error.contains("1048576 bytes"));
        QVERIFY(error.contains("16 MiB"));
    }
    void cellJsonRejectsInvalidUnicodeAndClearsOldDocument() {
        ResultTableModel model;
        QString invalid = "{\"x\":\"";
        invalid.append(QChar(0xd800));
        invalid += "\"}";
        QVERIFY(model.setPage({column("document", "jsonb")}, {{invalid}}, 0));
        QString json = "stale", error;
        QCOMPARE(model.cellJsonReadiness(model.index(0, 0), &error),
                 ResultTableModel::CellJsonReadiness::Invalid);
        QVERIFY(!model.cellJson(model.index(0, 0), &json, &error));
        QVERIFY(json.isEmpty());
        QVERIFY(!error.isEmpty());
    }
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
    void resultHeaderSeparatesTypeAndKeyMetadata() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("id", "bigint"), column("name", "text")}, {}, 0));
        QCOMPARE(model.headerData(0, Qt::Horizontal, ResultTableModel::HeaderTypeRole).toString(),
                 QString("bigint"));
        QCOMPARE(model.headerData(0, Qt::Horizontal, ResultTableModel::HeaderNameRole).toString(),
                 QString("id"));
        model.setKeyColumns({true, false});
        QCOMPARE(model.headerData(0, Qt::Horizontal, ResultTableModel::HeaderKeyRole).toBool(),
                 true);
        QCOMPARE(model.headerData(1, Qt::Horizontal, ResultTableModel::HeaderKeyRole).toBool(),
                 false);
        QVERIFY(model.setPage({column("other", "uuid")}, {}, 0));
        QCOMPARE(model.headerData(0, Qt::Horizontal, ResultTableModel::HeaderKeyRole).toBool(),
                 false);
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
    void fallbackTextIsDistinctReadOnlyAndCopiesCompleteServerText() {
        ResultTableModel model;
        const QString complete = QString(180, QChar('a')) + QStringLiteral("\tend");
        QVERIFY(model.setPage({column("native", "text"), column("unknown", "custom_type"),
                               column("empty", "custom_type")},
                              {{QString("native"), FallbackText{complete, "custom_type"},
                                FallbackText{QString(), "custom_type"}}},
                              0));
        model.setEditableColumns({true, true, true}, false, false);
        const auto fallback = model.index(0, 1);
        QCOMPARE(fallback.data(ResultTableModel::ResultValueKindRole).toString(),
                 QString("fallback_text"));
        QCOMPARE(fallback.data(ResultTableModel::ResultDatabaseTypeRole).toString(),
                 QString("custom_type"));
        QVERIFY(fallback.data().toString().contains("fallback", Qt::CaseInsensitive));
        QVERIFY(fallback.data().toString().size() < complete.size());
        QVERIFY(fallback.data(Qt::ToolTipRole).toString().contains("custom_type"));
        QVERIFY(!(model.flags(fallback) & Qt::ItemIsEditable));
        QVERIFY(!model.setData(fallback, "changed"));
        QVERIFY(!model.setNull(fallback));
        QCOMPARE(model.copyCells({fallback}), QStringLiteral("\"") + complete + '"');
        QCOMPARE(model.index(0, 2).data(ResultTableModel::ResultValueKindRole).toString(),
                 QString("fallback_text"));
        QVERIFY(model.index(0, 2).data().toString() != QString());
        QCOMPARE(model.copyCells({model.index(0, 2)}), QString());
    }
    void unavailableCellPreservesOtherCellsAndBlocksCopyAndUnsafeActions() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("normal", "text"), column("unreadable", "odd_type")},
                              {{QString("ok"), UnavailableValue{"odd_type", "text output failed"}}},
                              0));
        model.setEditableColumns({true, true}, true, true);
        const auto unavailable = model.index(0, 1);
        QCOMPARE(model.index(0, 0).data().toString(), QString("ok"));
        QCOMPARE(unavailable.data(ResultTableModel::ResultValueKindRole).toString(),
                 QString("unavailable"));
        QCOMPARE(unavailable.data(ResultTableModel::ResultUnavailableReasonRole).toString(),
                 QString("text output failed"));
        QVERIFY(unavailable.data().toString().contains("unavailable", Qt::CaseInsensitive));
        QVERIFY(unavailable.data(Qt::ToolTipRole).toString().contains("odd_type"));
        QVERIFY(!(model.flags(unavailable) & Qt::ItemIsEditable));
        QVERIFY(!model.setNull(unavailable));
        QString error;
        QVERIFY(model.copyCells({unavailable}, &error).isEmpty());
        QVERIFY(error.contains("odd_type"));
        QCOMPARE(model.copyCells({model.index(0, 0)}, &error), QString("ok"));
        QVERIFY(error.isEmpty());
        QVERIFY(model.copyRows({model.index(0, 0)}, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(model.copyPage(&error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(!model.duplicateRow(0, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!model.duplicateRow(0, {false, false}, &error));
        QVERIFY(error.contains("unavailable", Qt::CaseInsensitive));
    }
    void supportedCellsRemainEditableBesideFallbackAndOpaqueRowsCannotBeDeleted() {
        ResultTableModel model;
        QVERIFY(model.setPage(
            {column("id", "integer"), column("name", "text"), column("custom", "odd_type")},
            {{qint64(1), QString("before"), FallbackText{"(2,3)", "odd_type"}},
             {qint64(2), QString("plain"), std::monostate{}}},
            0));
        model.setEditableColumns({false, true, true}, false, true);
        QVERIFY(model.flags(model.index(0, 1)) & Qt::ItemIsEditable);
        QVERIFY(!(model.flags(model.index(0, 2)) & Qt::ItemIsEditable));
        QVERIFY(model.setData(model.index(0, 1), "after"));
        model.markDeleted({model.index(0, 0), model.index(1, 0)}, true);
        QVERIFY(!model.deleted()[0]);
        QVERIFY(model.deleted()[1]);
    }
    void rowJsonPreservesFallbackTypeAndRejectsUnavailable() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("ordinary", "text"), column("unfamiliar", "range_type")},
                              {{QString("ordinary"), FallbackText{"[1,9)", "range_type"}}}, 0));
        QString json, error;
        QCOMPARE(model.rowJsonReadiness(0, &error), ResultTableModel::RowJsonReadiness::Ready);
        QVERIFY(model.rowJson(0, &json, &error));
        const auto fallback =
            QJsonDocument::fromJson(json.toUtf8()).object().value("unfamiliar").toObject();
        QCOMPARE(fallback.value("fallback_text").toString(), QString("[1,9)"));
        QCOMPARE(fallback.value("database_type").toString(), QString("range_type"));
        QCOMPARE(model.pageJsonReadiness(&error), ResultTableModel::RowJsonReadiness::Ready);
        QVERIFY(model.pageJson(&json, &error));
        const auto pageFallback = QJsonDocument::fromJson(json.toUtf8())
                                      .array()
                                      .at(0)
                                      .toObject()
                                      .value("unfamiliar")
                                      .toObject();
        QCOMPARE(pageFallback.value("fallback_text").toString(), QString("[1,9)"));
        QVERIFY(model.setPage({column("ordinary", "text"), column("unfamiliar", "range_type")},
                              {{QString("ordinary"), UnavailableValue{"range_type", "failed"}}},
                              0));
        QCOMPARE(model.rowJsonReadiness(0, &error), ResultTableModel::RowJsonReadiness::Invalid);
        QVERIFY(error.contains("range_type"));
        QCOMPARE(model.pageJsonReadiness(&error), ResultTableModel::RowJsonReadiness::Invalid);
        QVERIFY(!model.rowJson(0, &json, &error));
        QVERIFY(json.isEmpty());
    }
    void deferredFallbackRetainsKindAndRequiresCompleteTextForRowJson() {
        ResultTableModel model;
        QVERIFY(model.setPage({column("unfamiliar", "range_type")},
                              {{DeferredValue{91, 5, "range_type", true}}}, 0));
        const auto index = model.index(0, 0);
        QCOMPARE(index.data(ResultTableModel::ResultValueKindRole).toString(),
                 QString("deferred_fallback"));
        QVERIFY(index.data().toString().contains("fallback", Qt::CaseInsensitive));
        QCOMPARE(model.deferredValue(index)->handle, quint64(91));
        QCOMPARE(model.rowJsonReadiness(0), ResultTableModel::RowJsonReadiness::NeedsDeferred);
        QString json, error;
        QVERIFY(!model.rowJson(0, &json, &error, {{0, QString("[1,9)")}}));
        QVERIFY(!error.isEmpty());
        QVERIFY(model.rowJson(0, &json, &error, {{0, FallbackText{"[1,9)", "range_type"}}}));
        const auto fallback =
            QJsonDocument::fromJson(json.toUtf8()).object().value("unfamiliar").toObject();
        QCOMPARE(fallback.value("fallback_text").toString(), QString("[1,9)"));
        QCOMPARE(fallback.value("database_type").toString(), QString("range_type"));
    }
    void loadedDeferredCopyUsesExactTypedValueAcrossScopes() {
        ResultTableModel model;
        QVERIFY(model.setPage(
            {column("fallback", "range_type"), column("binary", "blob"), column("text", "text")},
            {{DeferredValue{11, 5, "range_type", true}, DeferredValue{12, 3, "blob"},
              DeferredValue{13, 4, "text"}}},
            0));
        QString error;
        const ResultTableModel::ResolvedCells loaded = {
            {{0, 0}, FallbackText{"[1,9)", "range_type"}},
            {{0, 1}, QByteArray::fromHex("00ff7f")},
            {{0, 2}, QString("full")}};
        QCOMPARE(model.copyCells({model.index(0, 0)}, &error, loaded), QString("[1,9)"));
        QVERIFY(error.isEmpty());
        QCOMPARE(model.copyRows({model.index(0, 1)}, &error, loaded),
                 QString("[1,9)\t0x00ff7f\tfull"));
        QCOMPARE(model.copyPage(&error, loaded), QString("[1,9)\t0x00ff7f\tfull"));
        QVERIFY(model
                    .copyPage(&error, {{{0, 0}, FallbackText{"[1,9)", "wrong"}},
                                       {{0, 1}, QByteArray::fromHex("00ff7f")},
                                       {{0, 2}, QString("full")}})
                    .isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(model.copyPage(&error, {{{0, 0}, FallbackText{"[1,9)", "range_type"}}}).isEmpty());
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
    void copiedRealsMatchDisplayedPrecisionAndNotation() {
        const std::vector<double> values{0.0,
                                         -0.0,
                                         0.1,
                                         1.2345678901234567,
                                         0.0001,
                                         0.00001,
                                         1e16,
                                         1e17,
                                         std::numeric_limits<double>::infinity(),
                                         -std::numeric_limits<double>::infinity(),
                                         std::numeric_limits<double>::quiet_NaN()};
        std::vector<ResultTableModel::Row> rows;
        for (double value : values)
            rows.push_back({value});
        ResultTableModel model;
        QVERIFY(model.setPage({column("real", "double")}, std::move(rows), 0));
        for (int row = 0; row < model.rowCount(); ++row) {
            const auto index = model.index(row, 0);
            QCOMPARE(model.copyCells({index}), index.data().toString());
        }
    }
};
QTEST_MAIN(ResultModelTest)
#include "result_model_test.moc"
