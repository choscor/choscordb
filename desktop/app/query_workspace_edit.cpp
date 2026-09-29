#include "app/query_workspace.h"
#include "app/query_workspace_p.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/modal_panel/modal_panel.h"
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

namespace choscordb {
using query_workspace_detail::nextEditRequestToken;
using query_workspace_detail::text;
void QueryWorkspace::configureEditability() {
    if (widgets_.objectReadOnly && model_->columnCount() == static_cast<int>(editKey_.size()))
        model_->setKeyColumns(editKey_);
    if (editQualifiedName_.isEmpty() ||
        (!editReason_.isEmpty() &&
         !(widgets_.objectReadOnly && editReason_.contains("inserts only", Qt::CaseInsensitive))) ||
        model_->columnCount() != static_cast<int>(editColumnNames_.size()))
        return;
    std::vector<bool> editable(editColumnNames_.size()), insertEditable(editColumnNames_.size());
    std::vector<bool> opaqueColumns(editColumnNames_.size(), false);
    for (const auto& row : model_->rows())
        for (size_t i = 0; i < row.size(); ++i)
            opaqueColumns[i] = opaqueColumns[i] || std::holds_alternative<FallbackText>(row[i]) ||
                               std::holds_alternative<UnavailableValue>(row[i]);
    bool aligned = true, keyed = false;
    for (size_t i = 0; i < editable.size(); ++i) {
        aligned &= !widgets_.objectReadOnly || columns_[i].name == editColumnNames_[i];
        keyed |= editKey_[i];
        editable[i] = !opaqueColumns[i] && !editColumnNames_[i].isEmpty() && !editKey_[i] &&
                      !editGenerated_[i];
        insertEditable[i] =
            !opaqueColumns[i] && !editColumnNames_[i].isEmpty() && !editGenerated_[i];
    }
    if (!aligned) {
        editReason_ = tr("Result columns do not match table metadata.");
        return;
    }
    if (!keyed)
        std::fill(editable.begin(), editable.end(), false);
    for (size_t i = 0; i < editKey_.size(); ++i)
        if (editKey_[i] && opaqueColumns[i]) {
            keyed = false;
            std::fill(editable.begin(), editable.end(), false);
            break;
        }
    const auto comparableType = [](QString type) {
        type = type.toLower();
        return type == "integer" || type == "bigint" || type == "smallint" || type == "boolean" ||
               type == "text" || type == "uuid" || type == "date" ||
               type == "time without time zone" || type == "time with time zone" ||
               type.startsWith("timestamp") || type.startsWith("character") ||
               type.startsWith("varchar") || type.startsWith("numeric") ||
               type.startsWith("decimal") || type == "real" || type == "double precision" ||
               type == "jsonb";
    };
    if (editParameterStyle_ == "$")
        for (size_t i = 0; i < columns_.size(); ++i)
            if (!opaqueColumns[i] && !editColumnNames_[i].isEmpty() &&
                !comparableType(columns_[i].databaseType) &&
                !(i < cellMetadata_.size() && !cellMetadata_[i].enumChoices.isEmpty() &&
                  cellMetadata_[i].sourceColumn == editColumnNames_[i])) {
                keyed = false;
                std::fill(editable.begin(), editable.end(), false);
                break;
            }
    for (const auto& row : model_->rows())
        if (std::any_of(row.begin(), row.end(), [](const Cell& value) {
                return std::holds_alternative<DeferredValue>(value) ||
                       std::holds_alternative<QByteArray>(value);
            })) {
            keyed = false;
            std::fill(editable.begin(), editable.end(), false);
            editReason_ = tr("Binary or deferred original values prevent safe conflict checks; "
                             "inserts remain available.");
            break;
        }
    model_->setEditableColumns(std::move(editable), true, keyed, std::move(insertEditable));
    updateActions();
}
bool QueryWorkspace::applyStagedEdits() {
    if (!model_->hasPendingEdits())
        return true;
    if (!queryConnection_ || workInFlight() || editApplying_ || editQualifiedName_.isEmpty())
        return false;
    if (pendingTransactions_.contains(*queryConnection_) ||
        (widgets_.transactionActive && widgets_.transactionActive(*queryConnection_))) {
        message(tr("Commit or roll back the manual transaction before applying grid changes."));
        return false;
    }
    const bool mysql = driverForConnection(*queryConnection_) == QStringLiteral("mysql");
    const auto quoted = [mysql](QString name) {
        if (mysql) {
            name.replace('`', QStringLiteral("``"));
            return QStringLiteral("`") + name + QStringLiteral("`");
        }
        name.replace('"', QStringLiteral("\"\""));
        return QStringLiteral("\"") + name + QStringLiteral("\"");
    };
    const bool postgres = editParameterStyle_ == QStringLiteral("$");
    std::vector<ReviewedEditStatement> batch;
    QString review;
    const auto& rows = model_->rows();
    const auto& originals = model_->originalRows();
    const auto& touched = model_->touched();
    const auto& inserted = model_->inserted();
    const auto& deleted = model_->deleted();
    for (size_t r = 0; r < rows.size(); ++r) {
        if (!inserted[r] && !deleted[r] &&
            std::none_of(touched[r].begin(), touched[r].end(), [](bool v) { return v; }))
            continue;
        ReviewedEditStatement statement;
        const auto bind = [&](const Cell& value, size_t column) {
            statement.params.push_back(value);
            statement.paramTypes.push_back(columns_[column].databaseType);
            return postgres ? QStringLiteral("$") + QString::number(statement.params.size())
                            : QStringLiteral("?");
        };
        if (inserted[r] && deleted[r])
            continue;
        if (inserted[r]) {
            QStringList names, values;
            for (size_t c = 0; c < editColumnNames_.size(); ++c)
                if (touched[r][c] && !editGenerated_[c]) {
                    names << quoted(editColumnNames_[c]);
                    values << bind(rows[r][c], c);
                }
            statement.sql = names.isEmpty()
                                ? (mysql ? QStringLiteral("INSERT INTO %1 () VALUES ()")
                                         : QStringLiteral("INSERT INTO %1 DEFAULT VALUES"))
                                      .arg(editQualifiedName_)
                                : QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)")
                                      .arg(editQualifiedName_, names.join(", "), values.join(", "));
        } else {
            QStringList assignments, predicates;
            if (!deleted[r])
                for (size_t c = 0; c < editColumnNames_.size(); ++c)
                    if (touched[r][c] && !editKey_[c] && !editGenerated_[c])
                        assignments << quoted(editColumnNames_[c]) + " = " + bind(rows[r][c], c);
            if (assignments.isEmpty() && !deleted[r])
                continue;
            for (size_t c = 0; c < editColumnNames_.size(); ++c) {
                if (editColumnNames_[c].isEmpty())
                    continue;
                if (std::holds_alternative<DeferredValue>(originals[r][c]) ||
                    std::holds_alternative<QByteArray>(originals[r][c])) {
                    message(tr("Cannot safely compare a deferred or binary original value."));
                    return false;
                }
                if (std::holds_alternative<FallbackText>(originals[r][c]) ||
                    std::holds_alternative<UnavailableValue>(originals[r][c])) {
                    if (deleted[r] || editKey_[c]) {
                        message(tr("Cannot safely delete or match a row using a fallback or "
                                   "unavailable value."));
                        return false;
                    }
                    continue;
                }
                predicates << quoted(editColumnNames_[c]) +
                                  (postgres ? QStringLiteral(" IS NOT DISTINCT FROM ")
                                   : mysql  ? QStringLiteral(" <=> ")
                                            : QStringLiteral(" IS ")) +
                                  bind(originals[r][c], c);
            }
            statement.sql = deleted[r] ? QStringLiteral("DELETE FROM %1 WHERE %2")
                                             .arg(editQualifiedName_, predicates.join(" AND "))
                                       : QStringLiteral("UPDATE %1 SET %2 WHERE %3")
                                             .arg(editQualifiedName_, assignments.join(", "),
                                                  predicates.join(" AND "));
            statement.expectedRows = 1;
        }
        review += statement.sql + "\n";
        for (size_t i = 0; i < statement.params.size(); ++i) {
            const auto& value = statement.params[i];
            if (const auto* binary = std::get_if<QByteArray>(&value);
                binary && binary->size() > 65536) {
                message(tr("Binary parameter exceeds the 64 KiB review limit; narrow the edit."));
                return false;
            }
            QString shown = std::holds_alternative<std::monostate>(value) ? QStringLiteral("NULL")
                            : std::holds_alternative<QByteArray>(value)
                                ? QStringLiteral("binary 0x%1 (%2 bytes)")
                                      .arg(QString::fromLatin1(std::get<QByteArray>(value).toHex()))
                                      .arg(std::get<QByteArray>(value).size())
                                : std::visit(
                                      [](const auto& v) -> QString {
                                          using T = std::decay_t<decltype(v)>;
                                          if constexpr (std::is_same_v<T, QString>) {
                                              QString escaped = v;
                                              escaped.replace('\\', "\\\\");
                                              escaped.replace('"', "\\\"");
                                              escaped.replace('\n', "\\n");
                                              return QStringLiteral("text \"") + escaped + '"';
                                          } else if constexpr (std::is_same_v<T, bool>)
                                              return v ? "true" : "false";
                                          else if constexpr (std::is_same_v<T, DecimalValue>)
                                              return v.text;
                                          else if constexpr (std::is_arithmetic_v<T>)
                                              return QString::number(v);
                                          else
                                              return QString{};
                                      },
                                      value);
            review += tr("  Parameter %1: %2\n").arg(i + 1).arg(shown);
        }
        review += "\n";
        batch.push_back(std::move(statement));
    }
    if (batch.empty())
        return false;
    design::ModalDialog box(widgets_.dialogParent);
    box.setWindowTitle(tr("Review grid changes"));
    auto* layout = new QVBoxLayout(&box);
    layout->addWidget(new QLabel(tr("Statements and bound parameter values"), &box));
    auto* preview = new QPlainTextEdit(review, &box);
    preview->setObjectName("gridEditReview");
    preview->setReadOnly(true);
    layout->addWidget(preview);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &box);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    connect(buttons, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    layout->addWidget(buttons);
    if (widgets_.dialogParent)
        box.resize(widgets_.dialogParent->size() * (2.0 / 3.0));
    if (box.exec() != QDialog::Accepted || !queryConnection_ ||
        !connectionAvailable(*queryConnection_))
        return false;
    editApplyToken_ = nextEditRequestToken();
    editApplying_ = true;
    editApplied_ = false;
    if (!adapter_->applyEditBatch(*queryConnection_, batch, editApplyToken_)) {
        editApplying_ = false;
        return false;
    }
    QEventLoop loop;
    const auto connection =
        connect(adapter_, &EngineAdapter::eventReady, &loop, [this, &loop](const BridgeEvent& e) {
            if (e.request_token == editApplyToken_ &&
                (text(e.kind) == "edit_applied" || text(e.kind) == "edit_failed"))
                loop.quit();
        });
    if (editApplying_)
        loop.exec();
    disconnect(connection);
    return editApplied_;
}
} // namespace choscordb
