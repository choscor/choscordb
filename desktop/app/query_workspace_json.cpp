#include "app/query_workspace.h"
#include "app/query_workspace_p.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/json_text_view/json_text_view.h"
#include "design_system/right_sheet/right_sheet.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/value_detail_dialog/value_detail_dialog.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringDecoder>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

namespace choscordb {
using query_workspace_detail::text;
bool QueryWorkspace::jsonResultCurrent() const {
    if (jsonResultInvalidated_)
        return false;
    return !query_ || (currentPage_ && queryAvailable() && !fetching_ && !viewBusy_);
}
void QueryWorkspace::appendJsonViewActions(QMenu& menu, const QPersistentModelIndex& clicked,
                                           bool current) {
    current = current && jsonResultCurrent();
    const bool validTarget = current && clicked.isValid() && clicked.model() == model_ &&
                             clicked.row() >= 0 && clicked.row() < model_->rowCount();
    auto* viewCell = menu.addAction(tr("View cell as JSON"));
    viewCell->setObjectName("viewCellJson");
    bool cellAvailable = false;
    if (validTarget) {
        const auto readiness = model_->cellJsonReadiness(clicked);
        cellAvailable = readiness == ResultTableModel::CellJsonReadiness::Ready ||
                        readiness == ResultTableModel::CellJsonReadiness::Invalid ||
                        (readiness == ResultTableModel::CellJsonReadiness::NeedsDeferred &&
                         query_ && queryAvailable());
    }
    viewCell->setEnabled(cellAvailable);
    connect(viewCell, &QAction::triggered, this, [this, clicked] {
        if (clicked.isValid() && clicked.model() == model_ && widgets_.grid->model() == model_ &&
            widgets_.grid->selectionModel() && widgets_.grid->selectionModel()->model() == model_)
            openJsonView(JsonViewMode::Cell, clicked.row(), clicked.column());
    });
    auto* viewRow = menu.addAction(tr("View row as JSON"));
    viewRow->setObjectName("viewRowJson");
    bool rowAvailable = validTarget;
    if (rowAvailable) {
        const auto readiness = model_->rowJsonReadiness(clicked.row());
        rowAvailable = readiness == ResultTableModel::RowJsonReadiness::Ready ||
                       readiness == ResultTableModel::RowJsonReadiness::Invalid ||
                       (readiness == ResultTableModel::RowJsonReadiness::NeedsDeferred && query_ &&
                        queryAvailable());
    }
    viewRow->setEnabled(rowAvailable);
    connect(viewRow, &QAction::triggered, this, [this, clicked] {
        if (clicked.isValid() && clicked.model() == model_ && widgets_.grid->model() == model_ &&
            widgets_.grid->selectionModel() && widgets_.grid->selectionModel()->model() == model_)
            openJsonView(JsonViewMode::Row, clicked.row());
    });
    const QPersistentModelIndex pageMarker =
        current && model_->rowCount() > 0 && model_->columnCount() > 0
            ? QPersistentModelIndex(model_->index(0, 0))
            : QPersistentModelIndex{};
    auto* viewTable = menu.addAction(tr("View table as JSON"));
    viewTable->setObjectName("viewTableJson");
    viewTable->setEnabled(pageMarker.isValid());
    connect(viewTable, &QAction::triggered, this, [this, pageMarker] {
        if (pageMarker.isValid() && pageMarker.model() == model_ &&
            widgets_.grid->model() == model_ && widgets_.grid->selectionModel() &&
            widgets_.grid->selectionModel()->model() == model_)
            openJsonView(JsonViewMode::Table);
    });
}
void QueryWorkspace::clearResult() {
    clearRowJson();
    pendingCopy_.reset();
    if (export_)
        export_->clearQuery();
    if (detail_)
        detail_->clearValue();
    model_->setPage({}, {}, 0);
    cellMetadata_.clear();
    cellMetadataToken_ = 0;
    cellMetadataQuery_.reset();
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
void QueryWorkspace::clearRowJson() {
    rowJsonIndex_ = QPersistentModelIndex{};
    rowJsonMode_ = JsonViewMode::Row;
    rowJsonQuery_.reset();
    rowJsonResolved_.clear();
    rowJsonLoadingBytes_.clear();
    rowJsonLoadingKind_.clear();
    rowJsonExpectedKind_.clear();
    rowJsonLoadingColumn_ = -1;
    rowJsonLoadingRow_ = -1;
    rowJsonScanRow_ = -1;
    rowJsonScanColumn_ = -1;
    rowJsonLoadingHandle_ = rowJsonLoadingOffset_ = rowJsonLoadingTotal_ = 0;
    rowJsonResolvedBytes_ = 0;
    if (rowJsonCopy_)
        rowJsonCopy_->setEnabled(false);
    if (rowJsonText_)
        rowJsonText_->clear();
    if (rowJsonStatus_)
        rowJsonStatus_->clear();
    if (rowJsonSheet_ && rowJsonSheet_->isVisible())
        rowJsonSheet_->reject();
}
bool QueryWorkspace::serializeJsonView(QString* json, QString* error) const {
    if (rowJsonMode_ == JsonViewMode::Table)
        return model_->pageJson(json, error, rowJsonResolved_);
    if (!rowJsonIndex_.isValid()) {
        if (error)
            *error = tr("This result is no longer available.");
        return false;
    }
    if (rowJsonMode_ == JsonViewMode::Cell) {
        const auto found = rowJsonResolved_.find({rowJsonIndex_.row(), rowJsonIndex_.column()});
        const std::optional<Cell> resolved =
            found == rowJsonResolved_.end() ? std::nullopt : std::optional<Cell>{found->second};
        return model_->cellJson(rowJsonIndex_, json, error, resolved);
    }
    std::map<int, Cell> resolved;
    for (const auto& [position, value] : rowJsonResolved_)
        resolved.emplace(position.second, value);
    return model_->rowJson(rowJsonIndex_.row(), json, error, resolved);
}
void QueryWorkspace::openJsonView(JsonViewMode mode, int row, int column) {
    if (!jsonResultCurrent() || widgets_.grid->model() != model_ ||
        !widgets_.grid->selectionModel() || widgets_.grid->selectionModel()->model() != model_ ||
        row < 0 || row >= model_->rowCount() || column < 0 || column >= model_->columnCount())
        return;
    const auto target = model_->index(row, mode == JsonViewMode::Cell ? column : 0);
    if (!target.isValid())
        return;
    if (mode == JsonViewMode::Cell) {
        const auto readiness = model_->cellJsonReadiness(target);
        if (readiness == ResultTableModel::CellJsonReadiness::Unavailable ||
            (readiness == ResultTableModel::CellJsonReadiness::NeedsDeferred &&
             (!query_ || !queryAvailable())))
            return;
    } else if (mode == JsonViewMode::Row) {
        const auto readiness = model_->rowJsonReadiness(row);
        if (readiness == ResultTableModel::RowJsonReadiness::NeedsDeferred &&
            (!query_ || !queryAvailable()))
            return;
    }
    clearRowJson();
    if (!rowJsonSheet_) {
        rowJsonSheet_ = new design::RightSheet(widgets_.dialogParent);
        rowJsonSheet_->setObjectName("rowJsonSheet");
        auto* body = new QWidget(rowJsonSheet_);
        body->setAccessibleName(tr("JSON content"));
        auto* layout = new QVBoxLayout(body);
        rowJsonOmittedNote_ = new QLabel(tr("Omitted fields are shown as { \"$omitted\": true }; "
                                            "their database defaults have not been fetched."),
                                         body);
        rowJsonOmittedNote_->setWordWrap(true);
        rowJsonOmittedNote_->setAccessibleName(tr("Omitted field explanation"));
        rowJsonPageNote_ = new QLabel(
            tr("Only rows on the currently loaded page are included; other pages are excluded."),
            body);
        rowJsonPageNote_->setObjectName("rowJsonPageNote");
        rowJsonPageNote_->setWordWrap(true);
        rowJsonPageNote_->setAccessibleName(tr("JSON page scope"));
        rowJsonStatus_ = new QLabel(body);
        rowJsonStatus_->setObjectName("rowJsonStatus");
        rowJsonStatus_->setAccessibleName(tr("Row JSON status"));
        rowJsonStatus_->setWordWrap(true);
        rowJsonText_ = new design::JsonTextView(body);
        rowJsonText_->setObjectName("rowJsonText");
        rowJsonText_->setAccessibleName(tr("Row JSON text"));
        layout->addWidget(rowJsonPageNote_);
        layout->addWidget(rowJsonOmittedNote_);
        layout->addWidget(rowJsonStatus_);
        layout->addWidget(rowJsonText_, 1);
        rowJsonSheet_->setBody(body);
        rowJsonCopy_ = new design::Button(tr("Copy JSON"), rowJsonSheet_);
        rowJsonCopy_->setObjectName("rowJsonCopy");
        rowJsonCopy_->setAccessibleName(tr("Copy JSON"));
        rowJsonCopy_->setEnabled(false);
        rowJsonSheet_->footerLayout()->addWidget(rowJsonCopy_);
        connect(rowJsonCopy_, &QPushButton::clicked, this, [this] {
            if (rowJsonCopy_ && rowJsonCopy_->isEnabled() && rowJsonText_)
                QApplication::clipboard()->setText(rowJsonText_->toPlainText());
        });
        connect(rowJsonSheet_, &QDialog::finished, this, [this] { clearRowJson(); });
    }
    rowJsonMode_ = mode;
    rowJsonIndex_ = target;
    rowJsonScanRow_ = mode == JsonViewMode::Table ? 0 : row;
    rowJsonScanColumn_ = mode == JsonViewMode::Cell ? column : 0;
    const QString title = mode == JsonViewMode::Cell    ? tr("View cell as JSON")
                          : mode == JsonViewMode::Table ? tr("View table as JSON")
                                                        : tr("View row as JSON");
    rowJsonSheet_->setTitle(title);
    rowJsonSheet_->setAccessibleName(title);
    rowJsonText_->setAccessibleName(mode == JsonViewMode::Row ? tr("Row JSON text") : title);
    rowJsonStatus_->setAccessibleName(mode == JsonViewMode::Row ? tr("Row JSON status")
                                                                : tr("JSON view status"));
    rowJsonPageNote_->setVisible(mode == JsonViewMode::Table);
    rowJsonOmittedNote_->setVisible(mode != JsonViewMode::Cell);
    QString json, error;
    const bool ready = serializeJsonView(&json, &error);
    if (ready) {
        rowJsonText_->setPlainText(json);
        rowJsonCopy_->setEnabled(true);
        rowJsonStatus_->clear();
    } else {
        const bool deferred = mode == JsonViewMode::Cell
                                  ? model_->cellJsonReadiness(target) ==
                                        ResultTableModel::CellJsonReadiness::NeedsDeferred
                                  : (mode == JsonViewMode::Table
                                         ? model_->pageJsonReadiness() ==
                                               ResultTableModel::RowJsonReadiness::NeedsDeferred
                                         : model_->rowJsonReadiness(row) ==
                                               ResultTableModel::RowJsonReadiness::NeedsDeferred);
        if (!deferred || !query_ || !queryAvailable()) {
            rowJsonStatus_->setText(error.isEmpty() ? tr("The JSON cannot be read.") : error);
        } else {
            rowJsonQuery_ = query_;
            rowJsonStatus_->setText(tr("Loading complete JSON values…"));
        }
    }
    rowJsonSheet_->open();
    if (rowJsonQuery_)
        requestRowJsonChunk();
}
void QueryWorkspace::requestRowJsonChunk() {
    constexpr quint64 Limit = 8 * 1024 * 1024;
    if (!rowJsonSheet_ || !rowJsonSheet_->isVisible() || !rowJsonQuery_ ||
        !rowJsonIndex_.isValid() || rowJsonIndex_.model() != model_ || query_ != rowJsonQuery_ ||
        !queryAvailable()) {
        if (rowJsonQuery_)
            failRowJson(tr("The result page is no longer available."));
        return;
    }
    const int firstRow = rowJsonMode_ == JsonViewMode::Table ? 0 : rowJsonIndex_.row();
    const int lastRow = rowJsonMode_ == JsonViewMode::Table ? model_->rowCount() : firstRow + 1;
    const int firstColumn = rowJsonMode_ == JsonViewMode::Cell ? rowJsonIndex_.column() : 0;
    const int lastColumn =
        rowJsonMode_ == JsonViewMode::Cell ? firstColumn + 1 : model_->columnCount();
    if (rowJsonLoadingColumn_ < 0) {
        while (rowJsonScanRow_ < lastRow) {
            if (rowJsonScanColumn_ >= lastColumn) {
                ++rowJsonScanRow_;
                rowJsonScanColumn_ = firstColumn;
                continue;
            }
            const int row = rowJsonScanRow_;
            const int column = rowJsonScanColumn_++;
            const auto value = model_->deferredValue(model_->index(row, column));
            if (!value)
                continue;
            if (value->bytes > Limit || rowJsonResolvedBytes_ > Limit - value->bytes) {
                failRowJson(tr("The complete JSON view exceeds the 8 MiB loading limit."));
                return;
            }
            rowJsonLoadingRow_ = row;
            rowJsonLoadingColumn_ = column;
            rowJsonLoadingHandle_ = value->handle;
            rowJsonLoadingOffset_ = rowJsonLoadingTotal_ = 0;
            rowJsonLoadingBytes_.clear();
            rowJsonLoadingKind_.clear();
            const auto type = value->type.toLower();
            if (value->fallback || type == "text" || type == "string" || type == "json" ||
                type == "jsonb")
                rowJsonExpectedKind_ = QStringLiteral("text");
            else if (type == "binary" || type == "blob" || type == "bytea" ||
                     type == "varbinary")
                rowJsonExpectedKind_ = QStringLiteral("binary");
            else
                rowJsonExpectedKind_.clear();
            break;
        }
    }
    if (rowJsonLoadingColumn_ < 0) {
        QString json, error;
        if (!serializeJsonView(&json, &error)) {
            failRowJson(error);
            return;
        }
        rowJsonText_->setPlainText(json);
        rowJsonCopy_->setEnabled(true);
        rowJsonStatus_->clear();
        rowJsonQuery_.reset();
        rowJsonResolved_.clear();
        return;
    }
    rowJsonStatus_->setText(tr("Loading complete JSON values… %1 bytes")
                                .arg(rowJsonResolvedBytes_ + rowJsonLoadingOffset_));
    adapter_->loadValueChunk(*rowJsonQuery_, rowJsonLoadingHandle_, rowJsonLoadingOffset_, 65536);
}
void QueryWorkspace::failRowJson(const QString& error) {
    rowJsonQuery_.reset();
    rowJsonResolved_.clear();
    rowJsonLoadingBytes_.clear();
    rowJsonLoadingKind_.clear();
    rowJsonExpectedKind_.clear();
    rowJsonLoadingColumn_ = -1;
    rowJsonLoadingRow_ = -1;
    rowJsonResolvedBytes_ = 0;
    if (rowJsonText_)
        rowJsonText_->clear();
    if (rowJsonCopy_)
        rowJsonCopy_->setEnabled(false);
    if (rowJsonStatus_)
        rowJsonStatus_->setText(tr("Unable to show complete JSON: %1").arg(error.left(1024)));
}
void QueryWorkspace::handleRowJsonEvent(const BridgeEvent& event) {
    if (!rowJsonQuery_ || rowJsonLoadingColumn_ < 0 || event.id != *rowJsonQuery_ ||
        event.value_handle != rowJsonLoadingHandle_ ||
        event.chunk_offset != rowJsonLoadingOffset_ || !rowJsonSheet_ ||
        !rowJsonSheet_->isVisible())
        return;
    const auto kind = text(event.kind);
    if (kind == "value_chunk_failed") {
        failRowJson(text(event.error));
        return;
    }
    if (kind != "value_chunk")
        return;
    constexpr quint64 Limit = 8 * 1024 * 1024;
    const auto chunkKind = text(event.chunk_kind);
    const quint64 chunkSize = event.chunk_bytes.size();
    if (!event.has_lease || (chunkKind != "text" && chunkKind != "binary") ||
        (!rowJsonLoadingKind_.isEmpty() && rowJsonLoadingKind_ != chunkKind) ||
        (!rowJsonExpectedKind_.isEmpty() && rowJsonExpectedKind_ != chunkKind) ||
        (rowJsonLoadingOffset_ > 0 && event.total_bytes != rowJsonLoadingTotal_) ||
        event.total_bytes > Limit - rowJsonResolvedBytes_ || chunkSize > 65536 ||
        event.chunk_offset > event.total_bytes ||
        chunkSize > event.total_bytes - event.chunk_offset ||
        (chunkSize == 0 && event.chunk_offset < event.total_bytes)) {
        failRowJson(tr("Invalid or oversized value chunk."));
        return;
    }
    if (!adapter_ || !adapter_->retainTransfer(event.lease_id, chunkSize)) {
        failRowJson(tr("The value exceeds the available memory budget."));
        return;
    }
    QByteArray chunk(reinterpret_cast<const char*>(event.chunk_bytes.data()),
                     static_cast<qsizetype>(chunkSize));
    rowJsonLoadingBytes_.append(chunk);
    chunk.clear();
    adapter_->releasePageLease(event.lease_id);
    rowJsonLoadingKind_ = chunkKind;
    rowJsonLoadingTotal_ = event.total_bytes;
    rowJsonLoadingOffset_ += chunkSize;
    if (rowJsonLoadingOffset_ == rowJsonLoadingTotal_) {
        if (chunkKind == "text") {
            QStringDecoder decoder(QStringDecoder::Utf8);
            const QString decoded = decoder(QByteArrayView(rowJsonLoadingBytes_));
            if (decoder.hasError()) {
                failRowJson(tr("The complete text value is not valid UTF-8."));
                return;
            }
            const auto deferred =
                model_->deferredValue(model_->index(rowJsonLoadingRow_, rowJsonLoadingColumn_));
            rowJsonResolved_[{rowJsonLoadingRow_, rowJsonLoadingColumn_}] =
                deferred && deferred->fallback ? Cell{FallbackText{decoded, deferred->type}}
                                               : Cell{decoded};
        } else {
            rowJsonResolved_[{rowJsonLoadingRow_, rowJsonLoadingColumn_}] =
                std::move(rowJsonLoadingBytes_);
        }
        rowJsonResolvedBytes_ += rowJsonLoadingTotal_;
        rowJsonLoadingBytes_.clear();
        rowJsonLoadingColumn_ = -1;
        rowJsonLoadingRow_ = -1;
    }
    QTimer::singleShot(0, this, &QueryWorkspace::requestRowJsonChunk);
}
} // namespace choscordb
