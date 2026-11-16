#include "app/query_settings.h"
#include "app/query_workspace.h"
#include "app/query_workspace_p.h"
#include "app/result_filter_bar.h"
#include "design_system/menu/menu.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSignalBlocker>
#include <QTabBar>
#include <algorithm>
#include <utility>

namespace choscordb {
using query_workspace_detail::nextEditRequestToken;
void QueryWorkspace::setupStatementResults() {
    auto* tabs = widgets_.statementResults;
    if (!tabs)
        return;
    tabs->setVisible(false);
    tabs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabs, &QTabBar::currentChanged, this, &QueryWorkspace::showStatementResult);
    connect(tabs, &QTabBar::customContextMenuRequested, this, [this, tabs](const QPoint& point) {
        const auto index = tabs->tabAt(point);
        if (index < 0 || index >= int(statementResults_.size()))
            return;
        const auto query = statementResults_[size_t(index)].query;
        QMenu menu(tabs);
        menu.setObjectName("statementResultMenu");
        auto* pin = menu.addAction(statementResults_[size_t(index)].pinned ? tr("Unpin result")
                                                                           : tr("Pin result"));
        pin->setObjectName("pinStatementResult");
        connect(pin, &QAction::triggered, this, [this, query] {
            for (auto& result : statementResults_)
                if (result.query == query)
                    result.pinned = !result.pinned;
            syncStatementTabs();
        });
        const QPointer<QueryWorkspace> self(this);
        design::execContextMenu(menu, tabs->mapToGlobal(point));
        if (self)
            updateActions();
    });
}

void QueryWorkspace::submitStatement() {
    const auto next = statementQueue_.takeFirst();
    const auto connection = *queryConnection_;
    clearViewState();
    const auto limited = EngineAdapter::applyRowLimit(
        next.sql, connectionRowLimits_.value(connection), driverForConnection(connection),
        connectionSqlModes_.value(connection));
    executedSql_ = limited.sql;
    appliedRowLimit_ = limited.applied;
    commandError_.clear();
    query_ = adapter_->execute(connection, executedSql_, connectionProfiles_.value(connection),
                               querySettings_->preferences());
    if (query_ && statementEditor_)
        statementEditor_->setProfileId(connectionProfiles_.value(connection));
    runningStatementLabel_ = query_ ? next.label : QString{};
    resultSetStatement_.clear();
    resultSet_ = 1;
    clearResult();
    currentPage_.reset();
    hasMore_ = false;
    fetching_ = false;
    busy_ = query_.has_value();
    executionFinished_ = !query_.has_value();
    cancellationPending_ = false;
    applyExecutionEvent(busy_ ? QueryEvent::Submit : QueryEvent::Fail,
                        busy_ ? QString{}
                              : tr("Could not submit the query: %1").arg(commandError_));
    if (!busy_) {
        statementQueue_.clear();
        if (!commandError_.isEmpty())
            message(commandError_);
    }
    updateActions();
}

QString QueryWorkspace::resultSetLabel() const {
    return tr("%1 · Result %2").arg(resultSetStatement_).arg(resultSet_);
}

void QueryWorkspace::recordStatementResult(const QString& outcome, const QString& error) {
    statementResults_.push_back({*query_, *queryConnection_,
                                 std::exchange(runningStatementLabel_, QString{}), executedSql_,
                                 outcome, error, columns_, false});
}

void QueryWorkspace::continueResultSets(const QString& outcome) {
    if (runningStatementLabel_.isEmpty() || !query_ || !queryConnection_)
        return;
    if (resultSetStatement_.isEmpty())
        resultSetStatement_ = runningStatementLabel_;
    runningStatementLabel_ = resultSetLabel();
    recordStatementResult(outcome, {});
    const auto next = adapter_->nextResultSet(*query_);
    if (!next) {
        resultSetStatement_.clear();
        resultSet_ = 1;
        syncStatementTabs();
        statementQueue_.clear();
        return;
    }
    query_ = next;
    ++resultSet_;
    runningStatementLabel_ = resultSetLabel();
    clearViewState();
    clearResult();
    currentPage_.reset();
    completedDurationMs_.reset();
    completedAffectedRows_.clear();
    executionFinished_ = false;
    busy_ = true;
    syncStatementTabs();
}

void QueryWorkspace::finishStatement(const QString& outcome, const QString& error) {
    if (runningStatementLabel_.isEmpty() || !query_ || !queryConnection_)
        return;
    if (!resultSetStatement_.isEmpty()) {
        runningStatementLabel_ = resultSetLabel();
        resultSetStatement_.clear();
        resultSet_ = 1;
    }
    recordStatementResult(outcome, error);
    if (!error.isEmpty() && !statementQueue_.isEmpty() && widgets_.stopOnFirstError &&
        widgets_.stopOnFirstError->isChecked()) {
        statementQueue_.clear();
        message(tr("Run all stopped at the failed statement."));
    }
    syncStatementTabs();
    if (!statementQueue_.isEmpty() && connectionAvailable(*queryConnection_))
        submitStatement();
    else
        statementQueue_.clear();
}

void QueryWorkspace::showStatementResult(int index) {
    if (index < 0 || index >= int(statementResults_.size()))
        return;
    const auto result = statementResults_[size_t(index)];
    if (query_ == result.query)
        return;
    if (workInFlight() || !connectionAvailable(result.connection) || !resolvePendingEdits()) {
        syncStatementTabs();
        return;
    }
    clearViewState();
    clearResult();
    query_ = result.query;
    queryConnection_ = result.connection;
    executedSql_ = result.sql;
    currentPage_.reset();
    hasMore_ = false;
    message(tr("%1: %2").arg(result.label, result.error.isEmpty() ? result.outcome : result.error));
    if (!result.error.isEmpty()) {
        setExecutionState(QueryState::Failed, result.error);
    } else if (result.columns.empty()) {
        setExecutionState(QueryState::CompletedEmpty, result.outcome);
    } else {
        columns_ = result.columns;
        if (filterBar_)
            filterBar_->setColumns(columns_);
        fetching_ = true;
        adapter_->fetchPageAt(*query_, 0);
        setExecutionState(QueryState::CompletedRows, result.outcome);
        inspectResultEdit();
        requestCellMetadata();
    }
    syncStatementTabs();
    updateActions();
}

void QueryWorkspace::releaseStatementResults(bool keepPinned) {
    std::erase_if(statementResults_, [this, keepPinned](const StatementResult& result) {
        if (keepPinned && result.pinned)
            return false;
        if (adapter_ && connectionAvailable(result.connection))
            adapter_->releaseQuery(result.query);
        if (query_ == result.query)
            query_.reset();
        return true;
    });
    syncStatementTabs();
}

void QueryWorkspace::dropStatementResults(quint64 connection) {
    statementQueue_.clear();
    std::erase_if(statementResults_, [connection](const StatementResult& result) {
        return result.connection == connection;
    });
    syncStatementTabs();
}

void QueryWorkspace::replaceStatementQuery(quint64 previous, std::optional<quint64> next) {
    std::erase_if(statementResults_, [previous, next](StatementResult& result) {
        if (result.query != previous)
            return false;
        if (!next)
            return true;
        result.query = *next;
        return false;
    });
    syncStatementTabs();
}

bool QueryWorkspace::statementResultQuery(quint64 query) const {
    return std::ranges::any_of(statementResults_, [query](const StatementResult& result) {
        return result.query == query;
    });
}

void QueryWorkspace::syncStatementTabs() {
    auto* tabs = widgets_.statementResults;
    if (!tabs)
        return;
    const QSignalBlocker blocker(tabs);
    while (tabs->count() > 0)
        tabs->removeTab(0);
    for (const auto& result : statementResults_) {
        const auto index =
            tabs->addTab(result.pinned ? tr("%1 · Pinned").arg(result.label) : result.label);
        tabs->setTabToolTip(index, result.error.isEmpty() ? result.outcome : result.error);
        if (query_ == result.query)
            tabs->setCurrentIndex(index);
    }
}

void QueryWorkspace::inspectResultEdit() {
    if (widgets_.objectReadOnly || executedSql_.isEmpty() || !queryConnection_)
        return;
    editTargetToken_ = nextEditRequestToken();
    QStringList names;
    for (const auto& column : columns_)
        names << column.name;
    adapter_->inspectQueryEdit(*queryConnection_, executedSql_, names, editTargetToken_);
}
}
