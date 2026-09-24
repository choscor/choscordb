#include "app/query_workspace.h"
#include "app/result_filter_bar.h"
#include "bridge/engine_adapter.h"
#include <QBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QStyle>
#include <QTableView>
#include <algorithm>

namespace choscordb {
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
                    if (std::any_of(model_->rows().begin(), model_->rows().end(),
                                    [column](const auto& row) {
                                        return column >= 0 &&
                                               column < static_cast<int>(row.size()) &&
                                               std::holds_alternative<DeferredValue>(row[column]);
                                    })) {
                        message(tr("Large deferred values cannot be sorted."));
                        return;
                    }
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
}

void QueryWorkspace::setExecutionState(const QString& state, const QString& detail) {
    widgets_.summary->setProperty("state", state);
    const auto status = detail.isEmpty() ? state : detail;
    const auto label =
        resultOrigin_.isEmpty() ? status : resultOrigin_ + QStringLiteral(" · ") + status;
    widgets_.summary->setText(label);
    widgets_.summary->setToolTip(label);
    widgets_.summary->setAccessibleName(tr("Execution status: %1").arg(label));
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
    if (filterBar_)
        filterBar_->reset();
    updateSortIndicator();
}

void QueryWorkspace::requestResultView(const QList<ResultFilterCondition>& filters,
                                       qint32 sortColumn, const QString& sortDirection) {
    if (!widgets_.objectReadOnly || !query_ || !queryAvailable() || workInFlight())
        return;
    if (model_->hasPendingEdits()) {
        deferredViewFilters_ = filters;
        deferredViewSortColumn_ = sortColumn;
        deferredViewSortDirection_ = sortDirection;
        deferredViewRequest_ = true;
        if (!resolvePendingEdits()) {
            deferredViewRequest_ = false;
            filterBar_->restoreApplied();
        }
        if (model_->hasPendingEdits() || editApplying_ || preserveViewOnRefresh_)
            return;
        deferredViewRequest_ = false;
    }
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
        return;
    }
    viewBusy_ = true;
    filterBar_->setBusy(true);
    setExecutionState(QStringLiteral("running"), tr("◷ Preparing result view…"));
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
