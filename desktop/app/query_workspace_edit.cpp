#include "app/query_workspace.h"
#include "app/query_workspace_p.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/fonts/fonts.h"
#include "design_system/metrics/metrics.h"
#include "design_system/modal_panel/modal_panel.h"
#include "design_system/text/text.h"
#include <QEventLoop>
#include <QFutureWatcher>
#include <QHBoxLayout>
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
    const auto originalCount = model.originalRowCount();
    const auto& touched = model.touched();
    const auto& inserted = model.inserted();
    const auto& deleted = model.deleted();
    if (rows.size() != touched.size() || rows.size() != inserted.size() ||
        rows.size() != deleted.size())
        return request;
    for (size_t index = 0; index < rows.size(); ++index) {
        if (!inserted[index] && index >= originalCount)
            return request;
        request.rows.push_back(
            {rows[index], inserted[index] ? ResultTableModel::Row{} : model.originalRow(index),
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
    if (const auto& blocked = editTransactionGuard().applyEdits; !blocked.isEmpty()) {
        message(blocked);
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
    const auto review = plan.review;
    if (batch.empty())
        return false;
    design::ModalDialog box(widgets_.dialogParent);
    box.setWindowTitle(tr("Review grid changes"));
    auto* layout = new QVBoxLayout(&box);
    layout->setContentsMargins(
        design::spacing(design::Spacing::Four), design::spacing(design::Spacing::Four),
        design::spacing(design::Spacing::Four), design::spacing(design::Spacing::Four));
    layout->setSpacing(design::spacing(design::Spacing::Three));
    layout->addWidget(new design::Text(tr("Statements and bound parameter values"), &box));
    auto* preview = new QPlainTextEdit(review, &box);
    preview->setObjectName("gridEditReview");
    preview->setReadOnly(true);
    preview->setProperty("designRole", "codePreview");
    preview->setFont(design::resolveTypography(design::TypographyRole::Monospace));
    layout->addWidget(preview);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = new design::Button(tr("Cancel"), &box);
    cancel->setObjectName("gridEditCancel");
    cancel->setVariant(design::ButtonVariant::Outline);
    cancel->setAutoDefault(false);
    auto* apply = new design::Button(tr("Apply"), &box);
    apply->setObjectName("gridEditApply");
    apply->setDefault(true);
    connect(cancel, &QPushButton::clicked, &box, &QDialog::reject);
    connect(apply, &QPushButton::clicked, &box, &QDialog::accept);
    buttons->addWidget(cancel);
    buttons->addWidget(apply);
    layout->addLayout(buttons);
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
