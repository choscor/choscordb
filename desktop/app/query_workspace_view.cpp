#include "app/query_workspace.h"
#include "app/query_workspace_p.h"
#include "app/result_filter_bar.h"
#include "bridge/engine_adapter.h"
#include "design_system/status_line/status_line.h"
#include "design_system/table/table_style.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QBoxLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QStyle>
#include <QTableView>
#include <QVariantMap>
#include <QtConcurrent>
#include <algorithm>

namespace choscordb {
void QueryWorkspace::activateForeignKey(const QModelIndex& index) {
    if (!queryConnection_ || !queryAvailable() || workInFlight() || stopping_ ||
        widgets_.grid->model() != model_)
        return;
    const auto metadata = model_->linkedColumn(index);
    const auto value = model_->cellValue(index);
    if (!metadata || !value)
        return;
    const auto predicate = EngineAdapter::foreignKeyPredicate(metadata->targetColumn, *value);
    if (!predicate)
        return;
    emit foreignKeyRequested(*queryConnection_, metadata->targetObject,
                             metadata->targetQualifiedName, *predicate);
}

bool QueryWorkspace::quickFilterAvailable(const QPersistentModelIndex& clicked) const {
    return filterBar_ && adapter_ && query_ && queryAvailable() && !workInFlight() && !stopping_ &&
           !referenceFilterFailed_ && clicked.isValid() && clicked.model() == model_ &&
           widgets_.grid->model() == model_ &&
           clicked.column() < static_cast<int>(model_->columns().size());
}

void QueryWorkspace::appendQuickFilterActions(QMenu& menu, const QPersistentModelIndex& clicked) {
    if (!filterBar_)
        return;
    auto* quick = menu.addMenu(tr("Quick Filter"));
    quick->setObjectName("resultQuickFilter");
    quick->setToolTipsVisible(true);
    if (!quickFilterAvailable(clicked)) {
        quick->menuAction()->setEnabled(false);
        quick->menuAction()->setToolTip(
            tr("Quick Filter requires an available cell and an idle result."));
        return;
    }
    const auto value = model_->cellValue(clicked);
    if (!value) {
        quick->menuAction()->setEnabled(false);
        quick->menuAction()->setToolTip(tr("This cell is unavailable."));
        return;
    }
    const auto options =
        EngineAdapter::quickFilterOptions(model_->columns()[clicked.column()].name, *value);
    bool enabled = false;
    for (const auto& option : options) {
        auto* action = quick->addAction(QString(option.label).replace('&', "&&"));
        action->setObjectName(
            QStringLiteral("resultQuickFilterOption%1").arg(static_cast<int>(option.operation)));
        action->setEnabled(option.enabled);
        action->setToolTip(option.reason);
        enabled |= option.enabled;
        connect(action, &QAction::triggered, this,
                [this, clicked, query = *query_, operation = option.operation] {
                    activateQuickFilter(clicked, query, operation);
                });
    }
    quick->menuAction()->setEnabled(enabled);
    if (!enabled && !options.isEmpty())
        quick->menuAction()->setToolTip(options.first().reason);
}

void QueryWorkspace::activateQuickFilter(const QPersistentModelIndex& clicked, quint64 query,
                                         CellFilterOperator operation) {
    if (query_ != query || !quickFilterAvailable(clicked))
        return;
    // Capture the staged typed value before edit review can discard it or refresh the model.
    const auto value = model_->cellValue(clicked);
    if (!value)
        return;
    const auto column = model_->columns()[clicked.column()].name;
    QStringList columns;
    for (const auto& item : model_->columns())
        columns << item.name;
    const auto filters = filterBar_->conditions();
    const auto draft = filters.isEmpty() ? QString{} : filters.first().value;
    auto* watcher = new QFutureWatcher<CellFilterComposition>(this);
    watcher->setObjectName("resultQuickFilterPreparation");
    quickFilterPreparing_ = true;
    filterBar_->setBusy(true);
    updateActions();
    connect(watcher, &QFutureWatcher<CellFilterComposition>::finished, this,
            [this, watcher, clicked, query, filters] {
                const auto result = watcher->result();
                watcher->deleteLater();
                quickFilterPreparing_ = false;
                filterBar_->setBusy(viewBusy_);
                updateActions();
                if (query_ != query || !quickFilterAvailable(clicked) ||
                    !filterBar_->draftMatches(filters))
                    return;
                if (!result.error.isEmpty()) {
                    filterBar_->showValidationError(result.error);
                    return;
                }
                filterBar_->setExpression(result.expression);
                if (!result.validationError.isEmpty()) {
                    filterBar_->showValidationError(result.validationError);
                    return;
                }
                requestResultView(filterBar_->conditions(), viewSortColumn_, viewSortDirection_);
            });
    watcher->setFuture(QtConcurrent::run([columns, draft, column, value = *value, operation] {
        return EngineAdapter::composeQuickFilter(columns, draft, column, value, operation);
    }));
}

void QueryWorkspace::requestCellMetadata() {
    if (!query_ || !queryConnection_ || columns_.empty() || !adapter_)
        return;
    cellMetadataToken_ = query_workspace_detail::nextEditRequestToken();
    cellMetadataQuery_ = query_;
    QStringList names;
    for (const auto& column : columns_)
        names << column.name;
    adapter_->inspectResultCells(*queryConnection_, widgets_.objectReadOnly ? objectId_ : QString{},
                                 widgets_.objectReadOnly ? QString{} : executedSql_, names,
                                 cellMetadataToken_);
}

void QueryWorkspace::setupResultViewControls() {
    widgets_.grid->horizontalHeader()->setSectionsClickable(widgets_.objectReadOnly);
    if (widgets_.objectReadOnly) {
        auto* resultParent = widgets_.grid->parentWidget();
        filterBar_ = new ResultFilterBar(resultParent ? resultParent : widgets_.grid);
        if (auto* layout =
                resultParent ? qobject_cast<QBoxLayout*>(resultParent->layout()) : nullptr) {
            const int index = layout->indexOf(widgets_.grid);
            if (index >= 0)
                layout->insertWidget(index, filterBar_);
        } else
            filterBar_->hide();
        connect(filterBar_, &ResultFilterBar::applyRequested, this,
                [this](const QList<ResultFilterCondition>& filters) {
                    requestResultView(filters, viewSortColumn_, viewSortDirection_);
                });
        connect(filterBar_, &ResultFilterBar::clearRequested, this,
                [this] { requestResultView({}, viewSortColumn_, viewSortDirection_); });
        connect(widgets_.grid->horizontalHeader(), &QHeaderView::sectionClicked, this,
                [this](int column) {
                    if (referenceFilterFailed_)
                        return;
                    // Rust rejects sorting on fallback, unavailable, or deferred values.
                    QString direction = QStringLiteral("ascending");
                    qint32 nextColumn = column;
                    if (viewSortColumn_ == column && viewSortDirection_ == "ascending")
                        direction = QStringLiteral("descending");
                    else if (viewSortColumn_ == column && viewSortDirection_ == "descending") {
                        nextColumn = -1;
                        direction.clear();
                    }
                    requestResultView(viewFilters_, nextColumn, direction);
                });
    }
}

EngineAdapter* QueryWorkspace::adapter() const {
    return adapter_.data();
}

void QueryWorkspace::message(const QString& value) {
    widgets_.messages->appendPlainText(value);
    if (resultEditor_ && widgets_.currentEditor() == resultEditor_) {
        auto saved = resultEditor_->property("resultStatus").toMap();
        if (saved.value("source").toString() == resultOrigin_) {
            saved.insert("messages", widgets_.messages->toPlainText());
            resultEditor_->setProperty("resultStatus", saved);
        }
    }
}

void QueryWorkspace::setExecutionState(const QString& state, const QString& detail,
                                       const ExecutionMetrics& metrics) {
    if (state == QLatin1String("queued")) {
        completedDurationMs_.reset();
        completedAffectedRows_.clear();
    }
    if (auto* editor = resultEditor_.data()) {
        editor->setProperty("resultStatus",
                            QVariantMap{{"state", state},
                                        {"detail", detail},
                                        {"source", resultOrigin_},
                                        {"duration", metrics.duration},
                                        {"page", metrics.page},
                                        {"rows", metrics.rows},
                                        {"memory", metrics.visibleSize},
                                        {"messages", widgets_.messages->toPlainText()}});
    }
    if (!widgets_.objectReadOnly && ((resultEditor_ && widgets_.currentEditor() != resultEditor_) ||
                                     (!resultEditor_ && !resultOrigin_.isEmpty())))
        return;
    presentExecutionState(state, detail, metrics, resultOrigin_);
}

void QueryWorkspace::setDocumentStatus(SqlEditor* editor, const QString& state,
                                       const QString& detail, const QString& source) {
    if (editor) {
        const auto messages =
            editor == widgets_.currentEditor()
                ? widgets_.messages->toPlainText()
                : editor->property("resultStatus").toMap().value("messages").toString();
        editor->setProperty("resultStatus",
                            QVariantMap{{"state", state},
                                        {"detail", detail},
                                        {"source", source},
                                        {"messages", messages + QStringLiteral("\n") + detail}});
    }
    if (editor == widgets_.currentEditor())
        presentExecutionState(state, detail, {}, source);
}

void QueryWorkspace::presentExecutionState(const QString& state, const QString& detail,
                                           const ExecutionMetrics& metrics, const QString& source) {
    widgets_.summary->setProperty("state", state);
    const auto status = detail.isEmpty() ? state : detail;
    if (auto* footer = qobject_cast<design::StatusLine*>(widgets_.summary->parentWidget())) {
        const auto semantic = state == "failed"      ? design::StatusLine::State::Error
                              : state == "completed" ? design::StatusLine::State::Success
                                                     : design::StatusLine::State::Neutral;
        footer->setBusy(state == "queued" || state == "running" || state == "cancelling");
        footer->setContent(
            {source, status, metrics.duration, metrics.visibleSize, metrics.page, metrics.rows},
            semantic);
        widgets_.summary->setAccessibleName(tr("Execution status: %1").arg(footer->toolTip()));
        emit executionStateChanged(state);
        return;
    }
    QStringList parts;
    if (!source.isEmpty())
        parts << source;
    parts << status;
    for (const auto& metric : {metrics.duration, metrics.page, metrics.rows, metrics.visibleSize})
        if (!metric.isEmpty())
            parts << metric;
    const auto fullSummary = parts.join(QStringLiteral(" · "));
    widgets_.summary->setProperty("fullSource", source);
    widgets_.summary->setText(widgets_.outcome ? source : fullSummary);
    widgets_.summary->setToolTip(fullSummary);
    widgets_.summary->setAccessibleName(tr("Execution status: %1").arg(fullSummary));
    if (widgets_.outcome) {
        widgets_.outcome->setProperty("fullOutcome", status);
        widgets_.outcome->setText(status);
        widgets_.outcome->setToolTip(fullSummary);
        widgets_.outcome->setAccessibleName(tr("Execution outcome: %1").arg(status));
    }
    const auto updateMetric = [&fullSummary](QLabel* label, const QString& value) {
        if (!label)
            return;
        label->setText(value);
        label->setToolTip(fullSummary);
        label->setAccessibleName(value);
        label->setVisible(!value.isEmpty());
    };
    updateMetric(widgets_.durationMetric, metrics.duration);
    updateMetric(widgets_.pageMetric, metrics.page);
    updateMetric(widgets_.rowsMetric, metrics.rows);
    updateMetric(widgets_.visibleSizeMetric, metrics.visibleSize);
    widgets_.summary->style()->unpolish(widgets_.summary);
    widgets_.summary->style()->polish(widgets_.summary);
    emit executionStateChanged(state);
}

void QueryWorkspace::clearViewState() {
    viewFilters_.clear();
    proposedViewFilters_.clear();
    deferredViewFilters_.clear();
    viewSortColumn_ = proposedViewSortColumn_ = deferredViewSortColumn_ = -1;
    viewSortDirection_.clear();
    proposedViewSortDirection_.clear();
    deferredViewSortDirection_.clear();
    viewBusy_ = deferredViewRequest_ = false;
    proposedFiltersFromDraft_ = false;
    preserveViewOnRefresh_ = false;
    viewRefreshQuery_.reset();
    initialFilterPending_ = false;
    referenceFilterPending_ = false;
    referenceFilterFailed_ = false;
    initialFilter_.clear();
    if (filterBar_)
        filterBar_->reset();
    updateSortIndicator();
}

void QueryWorkspace::requestResultView(const QList<ResultFilterCondition>& filters,
                                       qint32 sortColumn, const QString& sortDirection) {
    if (!widgets_.objectReadOnly || !query_ || !queryAvailable() || workInFlight() || stopping_)
        return;
    const auto originalQuery = query_;
    const auto originalConnection = queryConnection_;
    const auto originalObject = objectId_;
    if (model_->hasPendingEdits()) {
        deferredViewFilters_ = filters;
        deferredViewSortColumn_ = sortColumn;
        deferredViewSortDirection_ = sortDirection;
        deferredViewRequest_ = true;
        const QPointer<QueryWorkspace> self(this);
        const bool resolved = resolvePendingEdits();
        if (!self)
            return;
        if (!resolved) {
            deferredViewRequest_ = false;
            filterBar_->restoreApplied();
            return;
        }
        if (model_->hasPendingEdits() || editApplying_ || preserveViewOnRefresh_)
            return;
        deferredViewRequest_ = false;
    }
    if (query_ != originalQuery || queryConnection_ != originalConnection ||
        objectId_ != originalObject || !queryAvailable() || workInFlight() || stopping_)
        return;
    submitResultView(filters, sortColumn, sortDirection);
}

void QueryWorkspace::submitResultView(const QList<ResultFilterCondition>& filters,
                                      qint32 sortColumn, const QString& sortDirection) {
    if (!widgets_.objectReadOnly || !query_)
        return;
    proposedViewFilters_ = filters;
    proposedFiltersFromDraft_ = filterBar_->draftMatches(filters);
    proposedViewSortColumn_ = sortColumn;
    proposedViewSortDirection_ = sortDirection;
    commandError_.clear();
    const bool accepted =
        filters.isEmpty() && sortColumn < 0
            ? adapter_->clearResultView(*query_)
            : adapter_->applyResultView(*query_, filters, sortColumn, sortDirection);
    if (!accepted) {
        proposedViewFilters_ = viewFilters_;
        proposedViewSortColumn_ = viewSortColumn_;
        proposedViewSortDirection_ = viewSortDirection_;
        if (proposedFiltersFromDraft_)
            filterBar_->showValidationError(
                tr("Filters could not be applied. See Messages for details."));
        setExecutionState(QStringLiteral("failed"),
                          tr("Previous result view restored: %1").arg(commandError_));
        updateActions();
        return;
    }
    viewBusy_ = true;
    filterBar_->setBusy(true);
    setExecutionState(QStringLiteral("running"), tr("Preparing result view…"));
    updateActions();
}

void QueryWorkspace::updateSortIndicator() {
    auto* header = widgets_.grid->horizontalHeader();
    if (viewSortColumn_ < 0 || viewSortDirection_.isEmpty()) {
        header->setSortIndicatorShown(false);
        return;
    }
    header->setSortIndicator(viewSortColumn_, viewSortDirection_ == "ascending"
                                                  ? Qt::AscendingOrder
                                                  : Qt::DescendingOrder);
    header->setSortIndicatorShown(true);
}
} // namespace choscordb
