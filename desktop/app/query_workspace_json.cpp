#include "app/query_workspace.h"
#include "app/query_workspace_p.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/right_sheet/right_sheet.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/value_detail_dialog/value_detail_dialog.h"
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringDecoder>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

namespace choscordb {
using query_workspace_detail::text;
void QueryWorkspace::clearResult() {
    clearRowJson();
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
    rowJsonQuery_.reset();
    rowJsonResolved_.clear();
    rowJsonLoadingBytes_.clear();
    rowJsonLoadingKind_.clear();
    rowJsonExpectedKind_.clear();
    rowJsonLoadingColumn_ = -1;
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
void QueryWorkspace::openRowJson(int row) {
    if (widgets_.grid->model() != model_ || !widgets_.grid->selectionModel() ||
        widgets_.grid->selectionModel()->model() != model_ || row < 0 ||
        row >= model_->rowCount() || model_->columnCount() == 0)
        return;
    clearRowJson();
    if (!rowJsonSheet_) {
        rowJsonSheet_ = new design::RightSheet(widgets_.dialogParent);
        rowJsonSheet_->setObjectName("rowJsonSheet");
        rowJsonSheet_->setAccessibleName(tr("View row as JSON"));
        rowJsonSheet_->setTitle(tr("View row as JSON"));
        auto* body = new QWidget(rowJsonSheet_);
        body->setAccessibleName(tr("Row JSON content"));
        auto* layout = new QVBoxLayout(body);
        auto* note = new QLabel(tr("Omitted fields are shown as { \"$omitted\": true }; "
                                   "their database defaults have not been fetched."),
                                body);
        note->setWordWrap(true);
        note->setAccessibleName(tr("Omitted field explanation"));
        rowJsonStatus_ = new QLabel(body);
        rowJsonStatus_->setObjectName("rowJsonStatus");
        rowJsonStatus_->setAccessibleName(tr("Row JSON status"));
        rowJsonStatus_->setWordWrap(true);
        rowJsonText_ = new QPlainTextEdit(body);
        rowJsonText_->setObjectName("rowJsonText");
        rowJsonText_->setAccessibleName(tr("Row JSON text"));
        rowJsonText_->setReadOnly(true);
        layout->addWidget(note);
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
    rowJsonIndex_ = model_->index(row, 0);
    QString json, error;
    const bool ready = model_->rowJson(row, &json, &error);
    if (ready) {
        rowJsonText_->setPlainText(json);
        rowJsonCopy_->setEnabled(true);
        rowJsonStatus_->clear();
    } else {
        bool deferred = false;
        for (int column = 0; column < model_->columnCount(); ++column)
            deferred |= model_->deferredValue(model_->index(row, column)).has_value();
        if (!deferred || !query_ || !queryAvailable()) {
            rowJsonStatus_->setText(error.isEmpty() ? tr("The row cannot be read.") : error);
        } else {
            rowJsonQuery_ = query_;
            rowJsonStatus_->setText(tr("Loading complete row values…"));
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
            clearRowJson();
        return;
    }
    const int row = rowJsonIndex_.row();
    if (rowJsonLoadingColumn_ < 0) {
        for (int column = 0; column < model_->columnCount(); ++column) {
            if (rowJsonResolved_.contains(column))
                continue;
            const auto value = model_->deferredValue(model_->index(row, column));
            if (!value)
                continue;
            if (value->bytes > Limit || rowJsonResolvedBytes_ > Limit - value->bytes) {
                failRowJson(tr("The complete row exceeds the 8 MiB loading limit."));
                return;
            }
            rowJsonLoadingColumn_ = column;
            rowJsonLoadingHandle_ = value->handle;
            rowJsonLoadingOffset_ = rowJsonLoadingTotal_ = 0;
            rowJsonLoadingBytes_.clear();
            rowJsonLoadingKind_.clear();
            const auto type = value->type.toLower();
            rowJsonExpectedKind_ =
                (type == "binary" || type == "blob" || type == "bytea" || type == "varbinary")
                    ? QStringLiteral("binary")
                : (type == "text" || type == "string") ? QStringLiteral("text")
                                                       : QString{};
            break;
        }
    }
    if (rowJsonLoadingColumn_ < 0) {
        QString json, error;
        if (!model_->rowJson(row, &json, &error, rowJsonResolved_)) {
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
    rowJsonStatus_->setText(tr("Loading complete row values… %1 bytes")
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
            rowJsonResolved_[rowJsonLoadingColumn_] = decoded;
        } else {
            rowJsonResolved_[rowJsonLoadingColumn_] = std::move(rowJsonLoadingBytes_);
        }
        rowJsonResolvedBytes_ += rowJsonLoadingTotal_;
        rowJsonLoadingBytes_.clear();
        rowJsonLoadingColumn_ = -1;
    }
    QTimer::singleShot(0, this, &QueryWorkspace::requestRowJsonChunk);
}
} // namespace choscordb
