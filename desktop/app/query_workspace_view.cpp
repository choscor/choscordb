#include "app/query_workspace.h"
#include "app/query_workspace_p.h"
#include "app/result_filter_bar.h"
#include "bridge/engine_adapter.h"
#include "design_system/menu/menu.h"
#include "design_system/status_line/status_line.h"
#include "design_system/table/table_style.h"
#include "widgets/screen_state/screen_state.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QBoxLayout>
#include <QClipboard>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QStackedWidget>
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
    auto* quick = menu.addMenu(tr("Quick filter"));
    quick->setObjectName("resultQuickFilter");
    quick->setToolTipsVisible(true);
    if (!quickFilterAvailable(clicked)) {
        quick->menuAction()->setEnabled(false);
        quick->menuAction()->setToolTip(
            tr("Quick filter requires an available cell and an idle result."));
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
                requestResultView(filterBar_->conditions(), viewSorts_);
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
    widgets_.grid->horizontalHeader()->setSectionsClickable(true);
    QWidget* anchor = widgets_.grid;
    for (auto* ancestor = anchor->parentWidget(); ancestor; ancestor = ancestor->parentWidget())
        if (qobject_cast<QStackedWidget*>(ancestor)) {
            anchor = ancestor;
            break;
        }
    auto* resultParent = anchor->parentWidget();
    filterBar_ = new ResultFilterBar(resultParent ? resultParent : widgets_.grid);
    if (auto* layout = resultParent ? qobject_cast<QBoxLayout*>(resultParent->layout()) : nullptr) {
        const int index = layout->indexOf(anchor);
        if (index >= 0)
            layout->insertWidget(index, filterBar_);
    } else
        filterBar_->hide();
    connect(filterBar_, &ResultFilterBar::applyRequested, this,
            [this](const QList<ResultFilterCondition>& filters) {
                requestResultView(filters, viewSorts_);
            });
    connect(filterBar_, &ResultFilterBar::clearRequested, this,
            [this] { requestResultView({}, viewSorts_); });
    connect(widgets_.grid->horizontalHeader(), &QHeaderView::sectionClicked, this,
            [this](int column) {
                if (referenceFilterFailed_)
                    return;
                const auto next = EngineAdapter::toggleResultSort(
                    viewSorts_, static_cast<quint32>(column),
                    QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier));
                if (!next.error.isEmpty()) {
                    message(next.error);
                    return;
                }
                requestResultView(viewFilters_, next.sorts);
            });
    auto* header = widgets_.grid->horizontalHeader();
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QWidget::customContextMenuRequested, this, [this, header](const QPoint& at) {
        const int column = header->logicalIndexAt(at);
        if (column < 0 || !header->model())
            return;
        QMenu menu(header);
        menu.setObjectName("resultHeaderMenu");
        const bool sortable = !referenceFilterFailed_;
        const auto addSort = [&](const QString& label, const QList<ResultSortKey>& sorts) {
            auto* action = menu.addAction(label);
            action->setEnabled(sortable);
            connect(action, &QAction::triggered, this,
                    [this, sorts] { requestResultView(viewFilters_, sorts); });
        };
        const auto key = static_cast<quint32>(column);
        addSort(tr("Sort ascending"), {{key, QStringLiteral("ascending")}});
        addSort(tr("Sort descending"), {{key, QStringLiteral("descending")}});
        addSort(tr("Clear sort"), {});
        menu.actions().constLast()->setEnabled(sortable && !viewSorts_.isEmpty());
        menu.addSeparator();
        const auto name = header->model()
                              ->headerData(column, Qt::Horizontal, ResultTableModel::HeaderNameRole)
                              .toString();
        connect(menu.addAction(tr("Copy column name")), &QAction::triggered, this,
                [name] { QGuiApplication::clipboard()->setText(name); });
        connect(menu.addAction(tr("Hide column")), &QAction::triggered, this,
                [this, name] { setColumnHidden(name, true); });
        auto* columns = menu.addMenu(tr("Columns"));
        columns->setObjectName("resultColumnsMenu");
        populateColumnMenu(*columns);
        design::execContextMenu(menu, header->viewport()->mapToGlobal(at));
    });
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

void QueryWorkspace::setExecutionState(QueryState state, const QString& detail,
                                       const ExecutionMetrics& metrics) {
    if (state == QueryState::Queued) {
        completedDurationMs_.reset();
        completedAffectedRows_.clear();
    }
    if (auto* editor = resultEditor_.data()) {
        editor->setProperty("resultStatus",
                            QVariantMap{{"state", int(state)},
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

void QueryWorkspace::applyExecutionEvent(QueryEvent event, const QString& detail) {
    const auto current = documentState(resultEditor_);
    const auto next = EngineAdapter::queryTransition(current, event);
    if (!next.allowed) {
        message(next.error);
        return;
    }
    if (next.state != current)
        setExecutionState(next.state, detail);
}

bool QueryWorkspace::stepGridEdit(GridEditEvent event, const QString& detail) {
    const auto next = EngineAdapter::gridEditTransition(gridEdit_, event);
    if (!next.allowed) {
        message(next.error);
        return false;
    }
    gridEdit_ = next.state;
    if (const auto shown = EngineAdapter::gridEditPresentation(gridEdit_, detail);
        shown.tone == Tone::Error)
        message(shown.message);
    return true;
}

bool QueryWorkspace::gridEditBusy() const {
    return gridEdit_.edits == EditPhase::Planning || gridEdit_.edits == EditPhase::Applying;
}

QueryState QueryWorkspace::documentState(const SqlEditor* editor) const {
    if (!editor)
        return executionState_;
    const auto saved = editor->property("resultStatus").toMap();
    return QueryState(saved.value("state", int(QueryState::Disconnected)).toInt());
}

void QueryWorkspace::setDocumentStatus(SqlEditor* editor, QueryState state, const QString& detail,
                                       const QString& source) {
    if (editor) {
        const auto messages =
            editor == widgets_.currentEditor()
                ? widgets_.messages->toPlainText()
                : editor->property("resultStatus").toMap().value("messages").toString();
        editor->setProperty("resultStatus",
                            QVariantMap{{"state", int(state)},
                                        {"detail", detail},
                                        {"source", source},
                                        {"messages", messages + QStringLiteral("\n") + detail}});
    }
    if (editor == widgets_.currentEditor())
        presentExecutionState(state, detail, {},
                              source.isEmpty() ? documentTarget(editor) : source);
}

void QueryWorkspace::presentExecutionState(QueryState state, const QString& detail,
                                           const ExecutionMetrics& metrics, const QString& source) {
    executionState_ = state;
    const auto shown = detail.isEmpty() ? EngineAdapter::queryPresentation(state)
                                        : EngineAdapter::queryPresentation(state, detail);
    design::setStatusTone(widgets_.summary, statusLineState(shown.tone));
    const auto& status = shown.message;
    if (auto* footer = qobject_cast<design::StatusLine*>(widgets_.summary->parentWidget())) {
        footer->setBusy(shown.busy);
        footer->setContent(
            {source, status, metrics.duration, metrics.visibleSize, metrics.page, metrics.rows},
            statusLineState(shown.tone));
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
    emit executionStateChanged(state);
}

void QueryWorkspace::clearViewState() {
    viewFilters_.clear();
    proposedViewFilters_.clear();
    deferredViewFilters_.clear();
    viewSorts_.clear();
    proposedViewSorts_.clear();
    deferredViewSorts_.clear();
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
                                       const QList<ResultSortKey>& sorts) {
    if (!query_ || !queryAvailable() || workInFlight() || stopping_)
        return;
    const auto originalQuery = query_;
    const auto originalConnection = queryConnection_;
    const auto originalObject = objectId_;
    if (model_->hasPendingEdits()) {
        deferredViewFilters_ = filters;
        deferredViewSorts_ = sorts;
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
        if (model_->hasPendingEdits() || gridEdit_.edits == EditPhase::Applying ||
            preserveViewOnRefresh_)
            return;
        deferredViewRequest_ = false;
    }
    if (query_ != originalQuery || queryConnection_ != originalConnection ||
        objectId_ != originalObject || !queryAvailable() || workInFlight() || stopping_)
        return;
    submitResultView(filters, sorts);
}

void QueryWorkspace::submitResultView(const QList<ResultFilterCondition>& filters,
                                      const QList<ResultSortKey>& sorts) {
    if (!query_)
        return;
    proposedViewFilters_ = filters;
    proposedFiltersFromDraft_ = filterBar_->draftMatches(filters);
    proposedViewSorts_ = sorts;
    commandError_.clear();
    const bool accepted = filters.isEmpty() && sorts.isEmpty()
                              ? adapter_->clearResultView(*query_)
                              : adapter_->applyResultView(*query_, filters, sorts);
    if (!accepted) {
        proposedViewFilters_ = viewFilters_;
        proposedViewSorts_ = viewSorts_;
        if (proposedFiltersFromDraft_)
            filterBar_->showValidationError(
                tr("Filters could not be applied. See Messages for details."));
        setExecutionState(QueryState::Failed,
                          tr("Previous result view restored: %1").arg(commandError_));
        updateActions();
        return;
    }
    viewBusy_ = true;
    filterBar_->setBusy(true);
    setExecutionState(QueryState::Running, tr("Preparing result view…"));
    updateActions();
}

void QueryWorkspace::updateSortIndicator() {
    QHash<int, int> ranks;
    for (int index = 0; index < viewSorts_.size(); ++index)
        ranks.insert(int(viewSorts_[index].column),
                     viewSorts_[index].direction == QLatin1String("ascending") ? index + 1
                                                                               : -(index + 1));
    model_->setSortRanks(ranks);
    auto* header = widgets_.grid->horizontalHeader();
    header->setSortIndicatorShown(!viewSorts_.isEmpty());
    if (!viewSorts_.isEmpty())
        header->setSortIndicator(int(viewSorts_.first().column),
                                 ranks.value(int(viewSorts_.first().column)) > 0
                                     ? Qt::AscendingOrder
                                     : Qt::DescendingOrder);
}
}
