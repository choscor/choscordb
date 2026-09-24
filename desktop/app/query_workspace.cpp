#include "app/query_workspace.h"
#include "app/query_settings.h"
#include "app/query_workspace_p.h"
#include "bridge/engine_adapter.h"
#include "bridge/result_column_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/menu/menu.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include "widgets/value_detail_dialog/value_detail_dialog.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMessageBox>
#include <QPersistentModelIndex>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
namespace choscordb {
using query_workspace_detail::nextEditRequestToken;
using query_workspace_detail::text;
QueryWorkspace::QueryWorkspace(Widgets widgets, QObject* parent)
    : QObject(parent), widgets_(std::move(widgets)),
      adapter_(widgets_.sharedAdapter ? widgets_.sharedAdapter
                                      : new EngineAdapter(this, widgets_.storagePath)),
      model_(new ResultTableModel(this)) {
    widgets_.grid->setModel(model_);
    widgets_.grid->horizontalHeader()->setContextMenuPolicy(Qt::PreventContextMenu);
    widgets_.grid->verticalHeader()->setContextMenuPolicy(Qt::PreventContextMenu);
    connect(widgets_.grid->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { updateActions(); });
    connect(widgets_.grid->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex&, const QModelIndex&) { updateActions(); });
    setupResultViewControls();
    if (!widgets_.objectReadOnly)
        widgets_.grid->setToolTip(
            tr("Query results are read only because the source table and key cannot be verified."));
    querySettings_ = new QuerySettingsController(adapter_, widgets_.dialogParent, this);
    connect(querySettings_, &QuerySettingsController::readyChanged, this,
            [this](bool) { updateActions(); });
    connect(querySettings_, &QuerySettingsController::failed, this,
            [this](const QString& error) { message(tr("Query settings: %1").arg(error)); });
    connect(adapter_, &EngineAdapter::historyWriteFailed, this,
            [this](quint64, const QString& error) { message(tr("History: %1").arg(error)); });
    if (widgets_.exportResult)
        connect(widgets_.exportResult, &QPushButton::clicked, this, [this] {
            if (!query_ || exporting_ || !queryAvailable())
                return;
            if (!export_) {
                export_ = new ExportDialog(adapter_, widgets_.dialogParent);
                connect(export_, &ExportDialog::exportRunningChanged, this, [this](bool running) {
                    exporting_ = running;
                    updateActions();
                });
            }
            export_->setQuery(*query_);
            export_->setResultViewActive(!viewFilters_.isEmpty() || viewSortColumn_ >= 0);
            export_->show();
            export_->raise();
        });
    const auto openDetail = [this](const QModelIndex& index) {
        if (!query_ || !queryAvailable())
            return;
        const auto value = model_->deferredValue(index);
        if (!value)
            return;
        if (!detail_)
            detail_ = new ValueDetailDialog(adapter_, widgets_.dialogParent);
        detail_->openValue(*query_, value->handle, value->type, value->bytes);
    };
    connect(widgets_.grid, &QTableView::doubleClicked, this, openDetail);
    connect(widgets_.grid, &QTableView::activated, this, openDetail);
    connect(model_, &ResultTableModel::pendingEditsChanged, this,
            [this](bool) { updateActions(); });
    if (widgets_.addRow)
        connect(widgets_.addRow, &QPushButton::clicked, model_, &ResultTableModel::addRow);
    if (widgets_.deleteRows)
        connect(widgets_.deleteRows, &QPushButton::clicked, this, [this] {
            if (widgets_.grid->selectionModel())
                model_->markDeleted(widgets_.grid->selectionModel()->selectedIndexes(), true);
        });
    if (widgets_.restoreRows)
        connect(widgets_.restoreRows, &QPushButton::clicked, this, [this] {
            if (widgets_.grid->selectionModel())
                model_->markDeleted(widgets_.grid->selectionModel()->selectedIndexes(), false);
        });
    if (widgets_.setNull)
        connect(widgets_.setNull, &QPushButton::clicked, this, [this] {
            const auto selection = widgets_.grid->selectionModel()->selectedIndexes();
            for (const auto& index : selection)
                model_->setNull(index);
        });
    if (widgets_.discardEdits)
        connect(widgets_.discardEdits, &QPushButton::clicked, model_,
                &ResultTableModel::discardEdits);
    if (widgets_.applyEdits)
        connect(widgets_.applyEdits, &QPushButton::clicked, this,
                &QueryWorkspace::applyStagedEdits);
    connect(adapter_, &EngineAdapter::eventReady, this, &QueryWorkspace::handleEvent);
    connect(adapter_, &EngineAdapter::commandFailed, this, [this](const QString& error) {
        if (fetching_) {
            busy_ = false;
            fetching_ = false;
        }
        message(error);
        updateActions();
    });
    connect(widgets_.run, &QAction::triggered, this, &QueryWorkspace::execute);
    connect(widgets_.cancel, &QAction::triggered, this, [this] {
        if (query_ && viewBusy_) {
            if (adapter_->cancelResultView(*query_)) {
                setExecutionState(QStringLiteral("cancelling"), tr("◷ Cancelling result view…"));
                updateActions();
            }
            return;
        }
        if (query_ && queryAvailable()) {
            if (!adapter_->cancelQuery(*query_))
                return;
            cancellationPending_ = true;
            setExecutionState(QStringLiteral("cancelling"), tr("◷ Cancelling…"));
            updateActions();
            widgets_.cancel->setEnabled(false);
        }
    });
    connect(widgets_.commit, &QAction::triggered, this, [this] {
        if (auto c = selectedConnection(); c && connectionAvailable(*c) && !workInFlight())
            adapter_->commitTransaction(*c);
    });
    connect(widgets_.rollback, &QAction::triggered, this, [this] {
        if (auto c = selectedConnection(); c && connectionAvailable(*c) && !workInFlight())
            adapter_->rollbackTransaction(*c);
    });
    connect(widgets_.newConnection, &QAction::triggered, this, [this] { showProfiles(); });
    connect(widgets_.connections, &QComboBox::currentIndexChanged, this, [this] {
        if (auto* editor = widgets_.currentEditor()) {
            if (workInFlight()) {
                documentChanged();
                message(tr("Finish or cancel the active query before changing its connection."));
                return;
            }
            const auto value = widgets_.connections->currentData();
            editor->setConnectionTarget(
                value.isValid() ? std::optional<quint64>(value.toULongLong()) : std::nullopt,
                widgets_.connections->currentText());
            editor->setProfileId(value.isValid() ? connectionProfiles_.value(value.toULongLong())
                                                 : QString{});
        }
        const QSignalBlocker blocker(widgets_.mode);
        const auto c = selectedConnection();
        widgets_.mode->setCurrentIndex(c && manualModes_.value(*c, false) ? 1 : 0);
        updateActions();
    });
    connect(widgets_.mode, &QComboBox::currentIndexChanged, this, [this] {
        if (const auto c = selectedConnection()) {
            if (!connectionAvailable(*c) || workInFlight()) {
                const QSignalBlocker blocker(widgets_.mode);
                widgets_.mode->setCurrentIndex(manualModes_.value(*c, false) ? 1 : 0);
                return;
            }
            if (widgets_.mode->currentIndex() == 0 && pendingTransactions_.contains(*c)) {
                const QSignalBlocker blocker(widgets_.mode);
                widgets_.mode->setCurrentIndex(1);
                message(tr("Commit or roll back before enabling auto-commit."));
            } else
                manualModes_.insert(*c, widgets_.mode->currentIndex() == 1);
        }
        updateActions();
    });
    connect(widgets_.nextPage, &QPushButton::clicked, this, [this] {
        if (query_ && (hasMore_ || hasMoreResults_) && !fetching_ && queryAvailable() &&
            !exporting_) {
            if (!resolvePendingEdits())
                return;
            fetching_ = true;
            busy_ = true;
            updateActions();
            if (hasMore_) {
                adapter_->fetchPageAt(*query_, currentPage_.value_or(0) + 1);
            } else {
                hasMoreResults_ = false;
                currentPage_.reset();
                executionFinished_ = false;
                clearViewState();
                adapter_->nextResultSet(*query_);
            }
        }
    });
    if (widgets_.previousPage)
        connect(widgets_.previousPage, &QPushButton::clicked, this, [this] {
            if (query_ && currentPage_ && *currentPage_ > 0 && !fetching_ && queryAvailable() &&
                !exporting_) {
                if (!resolvePendingEdits())
                    return;
                fetching_ = true;
                updateActions();
                adapter_->fetchPageAt(*query_, *currentPage_ - 1);
            }
        });
    const auto copyScope = [this](int scope) {
        if (widgets_.grid->model() != model_ || !widgets_.grid->selectionModel() ||
            widgets_.grid->selectionModel()->model() != model_) {
            message(tr("The result page is no longer available."));
            return;
        }
        const auto selection =
            scope == 2 ? QModelIndexList{} : widgets_.grid->selectionModel()->selectedIndexes();
        if ((scope != 2 && selection.isEmpty()) || !model_->rowCount() || !model_->columnCount())
            return;
        QString error;
        const auto copied = scope == 0   ? model_->copyCells(selection, &error)
                            : scope == 1 ? model_->copyRows(selection, &error)
                                         : model_->copyPage(&error);
        if (!error.isEmpty())
            message(error);
        else
            QApplication::clipboard()->setText(copied);
    };
    auto* copy = new QShortcut(QKeySequence::Copy, widgets_.grid);
    copy->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copy, &QShortcut::activated, this, [copyScope] { copyScope(0); });
    widgets_.grid->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(
        widgets_.grid, &QTableView::customContextMenuRequested, this,
        [this, copyScope](const QPoint& point) {
            QMenu menu(widgets_.grid);
            menu.setToolTipsVisible(true);
            const bool current = widgets_.grid->model() == model_ &&
                                 widgets_.grid->selectionModel() &&
                                 widgets_.grid->selectionModel()->model() == model_;
            const bool selected = current && widgets_.grid->selectionModel()->hasSelection();
            const QPersistentModelIndex clicked = widgets_.grid->indexAt(point);
            const QStringList names = {"copySelectedCells", "copySelectedRows", "copyCurrentPage"};
            const QStringList labels = {tr("Copy selected cells"), tr("Copy selected rows"),
                                        tr("Copy current page")};
            for (int scope = 0; scope < 3; ++scope) {
                auto* action = menu.addAction(labels[scope]);
                action->setObjectName(names[scope]);
                action->setEnabled(scope == 2 ? current && model_->rowCount() > 0 &&
                                                    model_->columnCount() > 0
                                              : selected);
                connect(action, &QAction::triggered, this,
                        [copyScope, scope] { copyScope(scope); });
            }
            if (widgets_.addRow || widgets_.deleteRows || widgets_.restoreRows ||
                widgets_.setNull) {
                menu.addSeparator();
                if (widgets_.addRow) {
                    auto* duplicate = menu.addAction(tr("Duplicate row"));
                    duplicate->setObjectName("duplicateRow");
                    duplicate->setEnabled(current && clicked.isValid() && model_->canInsert() &&
                                          !workInFlight());
                    duplicate->setToolTip(model_->canInsert()
                                              ? tr("Duplicate the row under the pointer")
                                              : (editReason_.isEmpty()
                                                     ? tr("This result is read only.")
                                                     : editReason_));
                    connect(duplicate, &QAction::triggered, this, [this, clicked] {
                        if (!clicked.isValid() || clicked.model() != model_)
                            return;
                        QString error;
                        bool duplicated = false;
                        if (editKey_.size() == static_cast<size_t>(model_->columnCount())) {
                            std::vector<bool> copyable(editKey_.size());
                            for (size_t column = 0; column < copyable.size(); ++column)
                                copyable[column] = !editKey_[column] && !editGenerated_[column] &&
                                                   !editColumnNames_[column].isEmpty();
                            duplicated = model_->duplicateRow(clicked.row(), copyable, &error);
                        } else {
                            duplicated = model_->duplicateRow(clicked.row(), &error);
                        }
                        if (!duplicated && !error.isEmpty())
                            message(error);
                    });
                }
                for (auto* button : {widgets_.addRow, widgets_.deleteRows, widgets_.restoreRows,
                                     widgets_.setNull}) {
                    if (!button)
                        continue;
                    auto* action = menu.addAction(button->accessibleName());
                    action->setObjectName(button->objectName());
                    action->setEnabled(button->isEnabled());
                    action->setToolTip(button->toolTip());
                    connect(action, &QAction::triggered, button, &QPushButton::click);
                }
            }
            menu.addSeparator();
            menu.addAction(widgets_.cancel);
            design::execContextMenu(menu, widgets_.grid->viewport()->mapToGlobal(point));
        });
    updateActions();
}
QueryWorkspace::~QueryWorkspace() {
    disconnect(model_, nullptr, this, nullptr);
    clearResult();
    if (widgets_.objectReadOnly && adapter_ && query_)
        adapter_->releaseQuery(*query_);
    delete detail_;
    delete export_;
    delete profiles_;
}
void QueryWorkspace::clearResult() {
    if (export_)
        export_->clearQuery();
    if (detail_)
        detail_->clearValue();
    model_->setPage({}, {}, 0);
    if (visibleLease_) {
        const auto lease = *visibleLease_;
        visibleLease_.reset();
        if (adapter_)
            adapter_->releasePageLease(lease);
    }
    std::vector<ResultColumn>().swap(columns_);
    if (schemaLease_) {
        const auto lease = *schemaLease_;
        schemaLease_.reset();
        if (adapter_)
            adapter_->releasePageLease(lease);
    }
}
bool QueryWorkspace::resolvePendingEdits() {
    if (!model_->hasPendingEdits())
        return true;
    QMessageBox box(QMessageBox::Warning, tr("Pending grid changes"),
                    tr("Apply or discard the staged grid changes before continuing."),
                    QMessageBox::NoButton, widgets_.dialogParent);
    auto* apply = box.addButton(tr("Apply…"), QMessageBox::AcceptRole);
    auto* discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    auto* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.exec();
    if (box.clickedButton() == discard) {
        model_->discardEdits();
        return true;
    }
    if (box.clickedButton() == apply && widgets_.applyEdits) {
        return applyStagedEdits();
    }
    return false;
}
void QueryWorkspace::configureEditability() {
    if (editQualifiedName_.isEmpty() ||
        (!editReason_.isEmpty() &&
         !(widgets_.objectReadOnly && editReason_.contains("inserts only", Qt::CaseInsensitive))) ||
        model_->columnCount() != static_cast<int>(editColumnNames_.size()))
        return;
    std::vector<bool> editable(editColumnNames_.size()), insertEditable(editColumnNames_.size());
    bool aligned = true, keyed = false;
    for (size_t i = 0; i < editable.size(); ++i) {
        aligned &= !widgets_.objectReadOnly || columns_[i].name == editColumnNames_[i];
        keyed |= editKey_[i];
        editable[i] = !editColumnNames_[i].isEmpty() && !editKey_[i] && !editGenerated_[i];
        insertEditable[i] = !editColumnNames_[i].isEmpty() && !editGenerated_[i];
    }
    if (!aligned) {
        editReason_ = tr("Result columns do not match table metadata.");
        return;
    }
    if (!keyed)
        std::fill(editable.begin(), editable.end(), false);
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
            if (!editColumnNames_[i].isEmpty() && !comparableType(columns_[i].databaseType)) {
                keyed = false;
                std::fill(editable.begin(), editable.end(), false);
                editReason_ = tr("A column type cannot be compared safely for conflicts; inserts "
                                 "remain available.");
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
                if (std::holds_alternative<DeferredValue>(originals[r][c])) {
                    message(tr("Cannot safely compare a deferred original value. Load a narrower "
                               "result."));
                    return false;
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
    QDialog box(widgets_.dialogParent);
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
bool QueryWorkspace::connectionAvailable(quint64 connection) const {
    return connectionCanDisconnect(connection);
}
bool QueryWorkspace::connectionCanDisconnect(quint64 connection) const {
    return !disconnecting_.contains(connection) &&
           widgets_.connections->findData(QVariant::fromValue<qulonglong>(connection)) >= 0;
}
bool QueryWorkspace::queryAvailable() const {
    return queryConnection_ && connectionAvailable(*queryConnection_);
}
bool QueryWorkspace::workInFlight() const {
    return externalWork_ || executionModeToken_ != 0 || viewBusy_ ||
           ((cancellationPending_ || busy_ || fetching_ || exporting_) &&
            !(queryConnection_ && disconnecting_.contains(*queryConnection_)));
}
void QueryWorkspace::setExternalWork(bool busy) {
    if (externalWork_ == busy)
        return;
    externalWork_ = busy;
    updateActions();
}
void QueryWorkspace::disconnectConnection(quint64 connection) {
    if (!connectionCanDisconnect(connection) || confirmingDisconnects_.contains(connection))
        return;
    const auto index = widgets_.connections->findData(QVariant::fromValue<qulonglong>(connection));
    const auto label = widgets_.connections->itemText(index);
    const bool transaction = pendingTransactions_.contains(connection);
    const bool active = queryConnection_ == connection &&
                        (busy_ || fetching_ || viewBusy_ || exporting_ || !executionFinished_);
    QString notice =
        tr("Disconnect %1? Any uncommitted transaction will be rolled back.").arg(label);
    if (active)
        notice += tr(" Active query, fetch, or export work will be cancelled.");
    ConfirmationDialog box(QMessageBox::Warning, tr("Disconnect database session"), notice,
                           QMessageBox::NoButton, widgets_.dialogParent);
    box.setTextFormat(Qt::PlainText);
    auto* accept = box.addButton(transaction ? tr("Roll back and disconnect") : tr("Disconnect"),
                                 QMessageBox::DestructiveRole);
    auto* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    confirmingDisconnects_.insert(connection);
    box.exec();
    confirmingDisconnects_.remove(connection);
    if (box.clickedButton() != accept || !connectionCanDisconnect(connection))
        return;
    disconnecting_.insert(connection);
    updateActions();
    if (!adapter_->disconnectConnection(connection)) {
        disconnecting_.remove(connection);
        updateActions();
        return;
    }
    // Separate dialogs can otherwise submit work even with the toolbar disabled.
    if (queryConnection_ == connection) {
        if (export_)
            export_->clearQuery();
        if (detail_)
            detail_->clearValue();
    }
    updateActions();
}
void QueryWorkspace::showProfiles(const QString& profileId) {
    if (stopping_ || workInFlight()) {
        message(tr("Finish or cancel active database work before managing connections."));
        return;
    }
    const bool existingDialog = profiles_ != nullptr;
    if (!profiles_) {
        profiles_ = new ProfileDialog(adapter_, widgets_.dialogParent);
        connect(profiles_, &ProfileDialog::openQueryRequested, this,
                &QueryWorkspace::openQueryRequested);
        connect(profiles_, &ProfileDialog::connectionSubmitted, this,
                [this](const SavedProfile& profile, quint64 id, bool savedProfile) {
                    pendingConnections_.insert(id, profile.name);
                    connectionDrivers_.insert(id, profile.driver);
                    if (savedProfile)
                        connectionProfiles_.insert(id, profile.id);
                });
    }
    if (!profileId.isEmpty())
        profiles_->manageProfile(profileId, "edit");
    else if (existingDialog)
        profiles_->newProfile();
    profiles_->show();
    profiles_->raise();
}
void QueryWorkspace::manageSavedProfile(const QString& profileId, const QString& action) {
    if (stopping_ || workInFlight()) {
        message(tr("Finish or cancel active database work before managing connections."));
        return;
    }
    showProfiles(profileId);
    if (profiles_)
        profiles_->manageProfile(profileId, action);
}
void QueryWorkspace::showQuerySettings() {
    if (!stopping_)
        querySettings_->open();
}
void QueryWorkspace::trackQueryPreferencesSave(quint64 token) {
    querySettings_->trackSave(token);
}
void QueryWorkspace::applyQueryPreferences(const QueryPreferences& preferences) {
    querySettings_->applyConfirmed(preferences);
}
QueryPreferences QueryWorkspace::queryPreferences() const {
    return querySettings_->preferences();
}
void QueryWorkspace::openObjectData(quint64 connection, const QString& object, const QString& label,
                                    const QueryPreferences& preferences, const QString& kind,
                                    bool preserveView) {
    if (!widgets_.objectReadOnly || !adapter_ || workInFlight() || stopping_)
        return;
    if (!resolvePendingEdits())
        return;
    preserveViewOnRefresh_ = preserveView;
    viewRefreshQuery_.reset();
    if (!preserveView)
        clearViewState();
    if (query_)
        adapter_->releaseQuery(*query_);
    clearResult();
    currentPage_.reset();
    hasMore_ = false;
    hasMoreResults_ = false;
    fetching_ = false;
    invalidatePending_ = false;
    cancellationPending_ = false;
    queryConnection_ = connection;
    if (widgets_.connections->findData(QVariant::fromValue<qulonglong>(connection)) < 0) {
        const QSignalBlocker blocker(widgets_.connections);
        widgets_.connections->addItem(label, QVariant::fromValue<qulonglong>(connection));
    }
    resultOrigin_ = label;
    objectKind_ = kind;
    objectId_ = object;
    editQualifiedName_.clear();
    editReason_ = kind == QStringLiteral("table") ? tr("Checking table edit eligibility…")
                                                  : tr("Only base tables can be edited.");
    editColumnNames_.clear();
    editKey_.clear();
    editGenerated_.clear();
    widgets_.messages->clear();
    message(tr("Object data: %1").arg(label));
    query_ = adapter_->openObjectData(connection, object, preferences);
    if (preserveView && query_)
        viewRefreshQuery_ = query_;
    else if (preserveView && !query_) {
        preserveViewOnRefresh_ = false;
        clearViewState();
        message(tr("The previous result view was invalidated because refresh could not start."));
    }
    if (query_ && kind == QStringLiteral("table")) {
        editTargetToken_ = nextEditRequestToken();
        adapter_->inspectEditTarget(connection, object, editTargetToken_);
    }
    busy_ = query_.has_value();
    executionFinished_ = !busy_;
    setExecutionState(busy_ ? QStringLiteral("queued") : QStringLiteral("failed"),
                      busy_ ? tr("◷ Loading object data…")
                            : tr("! Object read could not be submitted"));
    updateActions();
}
void QueryWorkspace::invalidateResult() {
    if (!resolvePendingEdits())
        return;
    if (workInFlight()) {
        invalidatePending_ = true;
        if (exporting_ && export_)
            export_->clearQuery();
        if (adapter_ && query_ && viewBusy_)
            adapter_->cancelResultView(*query_);
        else if (adapter_ && query_ && (busy_ || fetching_))
            cancellationPending_ = adapter_->cancelQuery(*query_);
        setExecutionState(QStringLiteral("cancelling"), tr("◷ Cancelling…"));
        updateActions();
        return;
    }
    invalidatePending_ = false;
    cancellationPending_ = false;
    clearResult();
    if (adapter_ && query_)
        adapter_->releaseQuery(*query_);
    query_.reset();
    queryConnection_.reset();
    currentPage_.reset();
    hasMore_ = false;
    hasMoreResults_ = false;
    resultOrigin_.clear();
    clearViewState();
    setExecutionState(QStringLiteral("disconnected"), tr("Open Data to read an object."));
    updateActions();
}
void QueryWorkspace::connectSqlite(const QString& path) {
    if (auto id = adapter_->connectSqlite(path)) {
        pendingConnections_.insert(*id, path);
        connectionDrivers_.insert(*id, QStringLiteral("sqlite"));
    }
}
std::optional<quint64> QueryWorkspace::connectSavedProfile(const SavedProfile& profile) {
    if (stopping_ || workInFlight()) {
        message(tr("Finish or cancel active database work before connecting."));
        return std::nullopt;
    }
    const auto id = adapter_->connectProfile(profile);
    if (id) {
        pendingConnections_.insert(*id, profile.name);
        connectionProfiles_.insert(*id, profile.id);
        connectionDrivers_.insert(*id, profile.driver);
    }
    return id;
}
void QueryWorkspace::documentChanged() {
    auto* editor = widgets_.currentEditor();
    const auto target = editor ? editor->connectionTarget() : std::nullopt;
    const QSignalBlocker blocker(widgets_.connections);
    const int index =
        target ? widgets_.connections->findData(QVariant::fromValue<qulonglong>(*target)) : -1;
    QString label = tr("Disconnected — choose a SQL target");
    if (editor && !editor->targetLabel().isEmpty())
        label = tr("Disconnected — %1").arg(editor->targetLabel());
    else if (editor && !editor->property("profileId").toString().isEmpty())
        label = tr("Disconnected profile — %1").arg(editor->property("profileId").toString());
    widgets_.connections->setPlaceholderText(label);
    widgets_.connections->setCurrentIndex(index);
    widgets_.connections->setToolTip(
        index >= 0 ? tr("SQL document target: %1").arg(widgets_.connections->currentText())
                   : label);
    const QSignalBlocker modeBlocker(widgets_.mode);
    widgets_.mode->setCurrentIndex(target && manualModes_.value(*target, false) ? 1 : 0);
    updateActions();
    emit documentTargetChanged();
}
std::optional<quint64> QueryWorkspace::selectedConnection() const {
    if (widgets_.objectReadOnly)
        return queryConnection_;
    const auto* editor = widgets_.currentEditor();
    return editor ? editor->connectionTarget() : std::nullopt;
}
void QueryWorkspace::updateActions() {
    if (invalidatePending_ && !workInFlight()) {
        invalidateResult();
        return;
    }
    const auto selected = selectedConnection();
    const bool connected = selected && connectionAvailable(*selected);
    const bool inFlight = workInFlight();
    widgets_.connections->setEnabled(widgets_.connections->count() > 0 && !inFlight && !stopping_);
    widgets_.mode->setEnabled(connected && !inFlight);
    widgets_.run->setEnabled(connected && !inFlight && querySettings_->isReady());
    widgets_.cancel->setEnabled(!cancellationPending_ && query_.has_value() &&
                                (busy_ || viewBusy_ || executionModeToken_ != 0) &&
                                queryAvailable() &&
                                widgets_.summary->property("state") != "cancelling");
    widgets_.cancel->setText(widgets_.summary->property("state") == "cancelling" ? tr("Cancelling…")
                                                                                 : tr("Cancel"));
    if (widgets_.exportResult)
        widgets_.exportResult->setEnabled(query_ && currentPage_ && !inFlight && queryAvailable());
    const bool manual = widgets_.mode->currentIndex() == 1;
    widgets_.commit->setEnabled(connected && manual && !inFlight);
    widgets_.rollback->setEnabled(connected && manual && !inFlight);
    widgets_.nextPage->setEnabled(query_.has_value() && (hasMore_ || hasMoreResults_) &&
                                  !inFlight && queryAvailable());
    widgets_.nextPage->setToolTip(!hasMore_ && hasMoreResults_ ? tr("Next result")
                                                               : tr("Next page"));
    widgets_.nextPage->setAccessibleName(widgets_.nextPage->toolTip());
    if (widgets_.previousPage)
        widgets_.previousPage->setEnabled(query_ && currentPage_ && *currentPage_ > 0 &&
                                          !inFlight && queryAvailable());
    if (widgets_.addRow)
        widgets_.addRow->setEnabled(model_->canInsert() && !inFlight);
    if (widgets_.addRow)
        widgets_.addRow->setToolTip(
            model_->canInsert()
                ? widgets_.addRow->accessibleName()
                : (editReason_.isEmpty() ? tr("This result is read only.") : editReason_));
    const bool canRemoveRows =
        model_->canDelete() || std::any_of(model_->inserted().begin(), model_->inserted().end(),
                                           [](bool inserted) { return inserted; });
    if (widgets_.deleteRows)
        widgets_.deleteRows->setEnabled(canRemoveRows && model_->rowCount() && !inFlight);
    if (widgets_.deleteRows)
        widgets_.deleteRows->setToolTip(canRemoveRows || editReason_.isEmpty()
                                            ? widgets_.deleteRows->accessibleName()
                                            : editReason_);
    if (widgets_.restoreRows)
        widgets_.restoreRows->setEnabled(
            !inFlight && std::any_of(model_->deleted().begin(), model_->deleted().end(),
                                     [](bool deleted) { return deleted; }));
    if (widgets_.setNull) {
        const auto selection = widgets_.grid->selectionModel()->selectedIndexes();
        widgets_.setNull->setEnabled(
            !inFlight && std::any_of(selection.begin(), selection.end(), [this](const auto& index) {
                return model_->flags(index) & Qt::ItemIsEditable;
            }));
    }
    if (widgets_.applyEdits)
        widgets_.applyEdits->setEnabled(
            model_->hasPendingEdits() && !inFlight &&
            !(queryConnection_ &&
              (pendingTransactions_.contains(*queryConnection_) ||
               (widgets_.transactionActive && widgets_.transactionActive(*queryConnection_)))));
    if (widgets_.applyEdits)
        widgets_.applyEdits->setToolTip(
            queryConnection_ &&
                    (pendingTransactions_.contains(*queryConnection_) ||
                     (widgets_.transactionActive && widgets_.transactionActive(*queryConnection_)))
                ? tr("Commit or roll back the manual transaction before applying grid changes.")
                : widgets_.applyEdits->accessibleName());
    if (widgets_.discardEdits)
        widgets_.discardEdits->setEnabled(model_->hasPendingEdits() && !inFlight);
    emit activityChanged(inFlight);
}
void QueryWorkspace::execute() {
    if (widgets_.objectReadOnly)
        return;
    auto connection = selectedConnection();
    auto* editor = widgets_.currentEditor();
    if (!connection || !editor || !connectionAvailable(*connection) || workInFlight() ||
        !querySettings_->isReady())
        return;
    if (!resolvePendingEdits())
        return;
    if (driverForConnection(*connection) == "mysql" && !executionModeReady_) {
        if (executionModeToken_ != 0)
            return;
        executionModeToken_ = nextEditRequestToken();
        executionModeEditor_ = editor;
        executionModeSql_ = editor->text();
        executionModeCursor_ =
            static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS));
        executionModeStart_ =
            static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONSTART));
        executionModeEnd_ =
            static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONEND));
        executionModeConnection_ = connection;
        if (!adapter_->refreshSqlMode(*connection, executionModeToken_)) {
            executionModeToken_ = 0;
            executionModeConnection_.reset();
        }
        updateActions();
        return;
    }
    const auto sql = editor->text();
    const auto range = EngineAdapter::executionRange(
        sql, static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS)),
        static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONSTART)),
        static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONEND)),
        driverForConnection(*connection), connectionSqlModes_.value(*connection));
    if (!range.valid) {
        message(tr("No executable statement at the cursor."));
        return;
    }
    if (range.confirmation) {
        ConfirmationDialog box(QMessageBox::Warning, tr("Confirm SQL execution"),
                               tr("This statement may change or delete data. Execute it?"),
                               QMessageBox::Yes | QMessageBox::Cancel, widgets_.dialogParent);
        box.setTextFormat(Qt::PlainText);
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Yes)
            return;
    }
    // Confirmation runs a nested event loop: the target may have disconnected.
    if (!connectionAvailable(*connection) || workInFlight() || widgets_.currentEditor() != editor)
        return;
    if (query_ && queryAvailable())
        adapter_->releaseQuery(*query_);
    clearViewState();
    const auto bytes = sql.toUtf8();
    executedSql_ = QString::fromUtf8(bytes.mid(static_cast<qsizetype>(range.start),
                                               static_cast<qsizetype>(range.end - range.start)));
    query_ =
        adapter_->execute(*connection, executedSql_, widgets_.mode->currentIndex() != 1,
                          connectionProfiles_.value(*connection), querySettings_->preferences());
    if (query_)
        editor->setProfileId(connectionProfiles_.value(*connection));
    if (query_ && widgets_.mode->currentIndex() == 1) {
        pendingTransactions_.insert(*connection);
        emit transactionStateChanged(*connection, true);
    }
    queryConnection_ = connection;
    auto title = editor->filePath().isEmpty() ? editor->property("documentTitle").toString()
                                              : QFileInfo(editor->filePath()).fileName();
    if (title.isEmpty())
        title = tr("Untitled query");
    resultOrigin_ = tr("%1 — %2").arg(title, editor->targetLabel());
    widgets_.messages->clear();
    message(tr("SQL result: %1").arg(resultOrigin_));
    clearResult();
    currentPage_.reset();
    hasMore_ = false;
    hasMoreResults_ = false;
    fetching_ = false;
    busy_ = query_.has_value();
    executionFinished_ = !query_.has_value();
    cancellationPending_ = false;
    setExecutionState(busy_ ? QStringLiteral("queued") : QStringLiteral("failed"),
                      busy_ ? tr("◷ Queued") : tr("! Submission failed"));
    updateActions();
}
} // namespace choscordb
