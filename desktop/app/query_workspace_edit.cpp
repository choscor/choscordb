#include "app/query_workspace.h"
#include "app/query_workspace_p.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/modal_panel/modal_panel.h"
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace choscordb {
using query_workspace_detail::nextEditRequestToken;
using query_workspace_detail::text;
namespace {
GridEditRequest editRequest(const QString& driver, const QString& qualifiedName,
                            const QString& parameterStyle, const QString& reason,
                            bool objectReadOnly, const std::vector<ResultColumn>& resultColumns,
                            const std::vector<ResultCellMetadata>& metadata,
                            const std::vector<QString>& names, const std::vector<bool>& keys,
                            const std::vector<bool>& generated, const ResultTableModel& model) {
    GridEditRequest request;
    request.driver = driver;
    request.qualifiedName = qualifiedName;
    request.parameterStyle = parameterStyle;
    request.reason = reason;
    request.objectReadOnly = objectReadOnly;
    if (names.size() != resultColumns.size() || names.size() != keys.size() ||
        names.size() != generated.size())
        return request;
    for (size_t index = 0; index < names.size(); ++index) {
        GridEditColumn column;
        column.name = names[index];
        column.resultName = resultColumns[index].name;
        column.databaseType = resultColumns[index].databaseType;
        column.key = keys[index];
        column.generated = generated[index];
        if (index < metadata.size()) {
            column.enumSourceColumn = metadata[index].sourceColumn;
            column.enumChoices = metadata[index].enumChoices;
        }
        request.columns.push_back(std::move(column));
    }
    const auto& rows = model.rows();
    const auto& originals = model.originalRows();
    const auto& touched = model.touched();
    const auto& inserted = model.inserted();
    const auto& deleted = model.deleted();
    if (rows.size() != touched.size() || rows.size() != inserted.size() ||
        rows.size() != deleted.size())
        return request;
    for (size_t index = 0; index < rows.size(); ++index) {
        if (!inserted[index] && index >= originals.size())
            return request;
        request.rows.push_back({rows[index],
                                inserted[index] ? ResultTableModel::Row{} : originals[index],
                                touched[index], inserted[index], deleted[index]});
    }
    return request;
}
} // namespace
void QueryWorkspace::configureEditability() {
    const auto generation = ++editPolicyGeneration_;
    const auto job = ++editabilityJobToken_;
    if (widgets_.objectReadOnly && model_->columnCount() == static_cast<int>(editKey_.size()))
        model_->setKeyColumns(editKey_);
    if (!queryConnection_ || model_->columnCount() != static_cast<int>(editColumnNames_.size())) {
        editabilityPlanning_ = false;
        updateActions();
        return;
    }
    const auto connection = *queryConnection_;
    const auto query = query_;
    const auto targetToken = editTargetToken_;
    const auto qualifiedName = editQualifiedName_;
    auto request = editRequest(driverForConnection(*queryConnection_), editQualifiedName_,
                               editParameterStyle_, editReason_, widgets_.objectReadOnly, columns_,
                               cellMetadata_, editColumnNames_, editKey_, editGenerated_, *model_);
    editabilityPlanning_ = true;
    updateActions();
    auto* watcher = new QFutureWatcher<GridEditEligibility>(this);
    connect(watcher, &QFutureWatcher<GridEditEligibility>::finished, this,
            [this, watcher, job, generation, connection, query, targetToken, qualifiedName] {
                auto eligibility = watcher->result();
                watcher->deleteLater();
                if (job != editabilityJobToken_)
                    return;
                editabilityPlanning_ = false;
                if (generation != editPolicyGeneration_ || queryConnection_ != connection ||
                    query_ != query || editTargetToken_ != targetToken ||
                    editQualifiedName_ != qualifiedName || stopping_ ||
                    !connectionAvailable(connection)) {
                    updateActions();
                    return;
                }
                if (!eligibility.reason.isEmpty())
                    editReason_ = eligibility.reason;
                if (eligibility.canInsert)
                    model_->setEditableColumns(std::move(eligibility.editable),
                                               eligibility.canInsert, eligibility.canDelete,
                                               std::move(eligibility.insertEditable));
                updateActions();
            });
    watcher->setFuture(QtConcurrent::run(
        [request = std::move(request)] { return EngineAdapter::gridEditability(request); }));
}
bool QueryWorkspace::applyStagedEdits() {
    if (!model_->hasPendingEdits())
        return true;
    if (!queryConnection_ || workInFlight() || editabilityPlanning_ || editApplying_ ||
        editQualifiedName_.isEmpty())
        return false;
    if (pendingTransactions_.contains(*queryConnection_) ||
        (widgets_.transactionActive && widgets_.transactionActive(*queryConnection_))) {
        message(tr("Commit or roll back the manual transaction before applying grid changes."));
        return false;
    }
    auto request = editRequest(driverForConnection(*queryConnection_), editQualifiedName_,
                               editParameterStyle_, editReason_, widgets_.objectReadOnly, columns_,
                               cellMetadata_, editColumnNames_, editKey_, editGenerated_, *model_);
    const auto generation = editPolicyGeneration_;
    const auto connectionId = *queryConnection_;
    const auto queryId = query_;
    const auto targetToken = editTargetToken_;
    const auto qualifiedName = editQualifiedName_;
    QPointer<QueryWorkspace> self(this);
    QEventLoop planningLoop;
    QFutureWatcher<std::shared_ptr<GridEditPlan>> planner;
    connect(&planner, &QFutureWatcher<std::shared_ptr<GridEditPlan>>::finished, &planningLoop,
            &QEventLoop::quit);
    connect(this, &QObject::destroyed, &planningLoop, &QEventLoop::quit);
    editPlanRunning_ = true;
    emit gridEditPlanningChanged(true);
    updateActions();
    planner.setFuture(QtConcurrent::run([request = std::move(request)] {
        return std::make_shared<GridEditPlan>(EngineAdapter::planGridEdits(request));
    }));
    if (!planner.isFinished())
        planningLoop.exec();
    if (!self)
        return false;
    editPlanRunning_ = false;
    emit gridEditPlanningChanged(false);
    updateActions();
    if (!planner.isFinished() || generation != editPolicyGeneration_ ||
        queryConnection_ != connectionId || query_ != queryId || editTargetToken_ != targetToken ||
        editQualifiedName_ != qualifiedName || stopping_ || !connectionAvailable(connectionId) ||
        workInFlight())
        return false;
    auto plan = std::move(*planner.result());
    if (!plan.error.isEmpty()) {
        message(plan.error);
        return false;
    }
    auto batch = std::move(plan.statements);
    QString review;
    for (const auto& statement : batch) {
        review += statement.sql + "\n";
        for (size_t i = 0; i < statement.params.size(); ++i) {
            const auto& value = statement.params[i];
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
    const auto accepted = box.exec() == QDialog::Accepted;
    if (!self)
        return false;
    if (!accepted || generation != editPolicyGeneration_ || queryConnection_ != connectionId ||
        query_ != queryId || editTargetToken_ != targetToken ||
        editQualifiedName_ != qualifiedName || !connectionAvailable(connectionId) || workInFlight())
        return false;
    editApplyToken_ = nextEditRequestToken();
    editApplying_ = true;
    editApplied_ = false;
    updateActions();
    QEventLoop loop;
    const auto eventConnection =
        connect(adapter_, &EngineAdapter::eventReady, &loop, [self, &loop](const BridgeEvent& e) {
            if (self && e.request_token == self->editApplyToken_ &&
                (text(e.kind) == "edit_applied" || text(e.kind) == "edit_failed"))
                loop.quit();
        });
    connect(this, &QObject::destroyed, &loop, &QEventLoop::quit);
    connect(adapter_, &QObject::destroyed, &loop, &QEventLoop::quit);
    if (!adapter_ || !adapter_->applyEditBatch(connectionId, batch, editApplyToken_)) {
        editApplying_ = false;
        updateActions();
        return false;
    }
    if (editApplying_)
        loop.exec();
    disconnect(eventConnection);
    if (!self)
        return false;
    if (!adapter_)
        editApplying_ = false;
    return editApplied_;
}
} // namespace choscordb
