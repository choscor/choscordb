#include "widgets/value_detail_dialog.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "models/value_preview_model.h"
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QStringDecoder>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>

namespace choscordb {
namespace {
QString text(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
constexpr quint32 ChunkBytes = 65536;
} // namespace
ValueDetailDialog::ValueDetailDialog(EngineAdapter* adapter, QWidget* parent)
    : QDialog(parent), adapter_(adapter), model_(new ValuePreviewModel(this)),
      table_(new QTableView(this)), status_(new QLabel(this)),
      previous_(new QPushButton(tr("Previous"), this)), next_(new QPushButton(tr("Next"), this)) {
    setObjectName("valueDetail");
    setWindowTitle(tr("Value detail"));
    setModal(false);
    resize(760, 480);
    status_->setObjectName("valueStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    previous_->setObjectName("valuePrevious");
    next_->setObjectName("valueNext");
    table_->setObjectName("valuePreview");
    table_->setModel(model_);
    table_->setWordWrap(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->addButton(previous_, QDialogButtonBox::ActionRole);
    buttons->addButton(next_, QDialogButtonBox::ActionRole);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(status_);
    layout->addWidget(table_);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &ValueDetailDialog::reject);
    connect(previous_, &QPushButton::clicked, this, &ValueDetailDialog::previousChunk);
    connect(table_->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] { sizeVisibleColumns(); });
    connect(next_, &QPushButton::clicked, this, [this] { request(nextOffset_); });
    connect(adapter, &EngineAdapter::eventReady, this, &ValueDetailDialog::handleEvent);
    connect(adapter, &EngineAdapter::valueChunkSubmissionFailed, this,
            [this](quint64 query, quint64 handle, quint64 offset, const QString& error) {
                if (loading_ && query_ == query && handle_ == handle && offset_ == offset)
                    fail(error);
            });
    updateActions();
}
ValueDetailDialog::~ValueDetailDialog() {
    dropChunk();
}
void ValueDetailDialog::openValue(quint64 query, quint64 handle, const QString& type,
                                  std::optional<quint64> length) {
    // Reopening an in-flight target keeps a single request for that target.
    if (query_ == query && handle_ == handle) {
        show();
        raise();
        activateWindow();
        return;
    }
    clearValue();
    query_ = query;
    handle_ = handle;
    total_ = length.value_or(0);
    setWindowTitle(type.isEmpty() ? tr("Value detail")
                                  : tr("Value detail — %1").arg(type.left(128)));
    show();
    request(0);
}
void ValueDetailDialog::dropChunk() {
    model_->clear();
    hasChunk_ = false;
    if (lease_) {
        const auto lease = *lease_;
        lease_.reset();
        if (adapter_)
            adapter_->releasePageLease(lease);
    }
}
void ValueDetailDialog::clearValue() {
    query_.reset();
    alignmentTarget_.reset();
    loading_ = false;
    dropChunk();
    offset_ = total_ = nextOffset_ = 0;
    windowBytes_ = ChunkBytes;
    status_->clear();
    updateActions();
    hide();
}
void ValueDetailDialog::closeEvent(QCloseEvent* event) {
    clearValue();
    QDialog::closeEvent(event);
}
void ValueDetailDialog::reject() {
    clearValue();
    QDialog::reject();
}
void ValueDetailDialog::sizeVisibleColumns() {
    const int first = std::max(0, table_->rowAt(0));
    int width = table_->columnWidth(1);
    for (int row = first; row < std::min(first + 32, model_->rowCount()); ++row)
        width = std::max(width, table_->fontMetrics().horizontalAdvance(
                                    model_->data(model_->index(row, 1)).toString()) +
                                    24);
    table_->setColumnWidth(1, width);
}
void ValueDetailDialog::previousChunk() {
    if (!query_ || loading_)
        return;
    const auto target = offset_ > windowBytes_ ? offset_ - windowBytes_ : 0;
    if (target == 0) {
        request(0);
        return;
    }
    // Seven bytes cover any UTF-8 character crossing the desired byte boundary.
    alignmentTarget_ = target;
    request(target > 3 ? target - 3 : 0, 7);
}
void ValueDetailDialog::request(quint64 offset, quint32 maxBytes) {
    if (!query_ || loading_)
        return;
    // Release the displayed allocation before waiting for another transfer reservation.
    dropChunk();
    offset_ = offset;
    loading_ = true;
    status_->setText(tr("Loading bytes at offset %1…").arg(offset));
    updateActions();
    if (adapter_)
        adapter_->loadValueChunk(*query_, handle_, offset, maxBytes);
    else
        fail(tr("The connection is no longer available."));
}
void ValueDetailDialog::fail(const QString& error) {
    loading_ = false;
    alignmentTarget_.reset();
    status_->setText(tr("Unable to load value: %1").arg(error.left(1024)));
    updateActions();
}
void ValueDetailDialog::handleEvent(const BridgeEvent& event) {
    if (!loading_ || !query_ || event.id != *query_ || event.value_handle != handle_ ||
        event.chunk_offset != offset_)
        return;
    const auto kind = text(event.kind);
    if (kind == "value_chunk_failed") {
        fail(text(event.error));
        return;
    }
    if (kind != "value_chunk")
        return;
    if (!event.has_lease || event.chunk_bytes.size() > ChunkBytes) {
        fail(tr("Invalid value chunk."));
        return;
    }
    const auto chunkKind = text(event.chunk_kind);
    if (alignmentTarget_) {
        auto target = *alignmentTarget_;
        if (chunkKind == "text") {
            const auto relative = target - event.chunk_offset;
            for (size_t i = 0; i < relative && i < event.chunk_bytes.size(); ++i) {
                const auto byte = event.chunk_bytes[i];
                const size_t length = byte >= 0xc2 && byte <= 0xdf   ? 2
                                      : byte >= 0xe0 && byte <= 0xef ? 3
                                      : byte >= 0xf0 && byte <= 0xf4 ? 4
                                                                     : 1;
                if (i + length > relative && i + length <= event.chunk_bytes.size()) {
                    QStringDecoder decoder(QStringDecoder::Utf8);
                    const QString decoded = decoder(QByteArrayView(
                        reinterpret_cast<const char*>(event.chunk_bytes.data() + i), length));
                    if (!decoder.hasError() && !decoded.isEmpty())
                        target = event.chunk_offset + i + length;
                    break;
                }
            }
        }
        alignmentTarget_.reset();
        loading_ = false;
        request(target);
        return;
    }
    if ((chunkKind != "text" && chunkKind != "binary") ||
        !model_->setChunk(QByteArray(reinterpret_cast<const char*>(event.chunk_bytes.data()),
                                     static_cast<qsizetype>(event.chunk_bytes.size())),
                          event.chunk_offset, event.total_bytes, chunkKind == "binary")) {
        fail(tr("Invalid value chunk."));
        return;
    }
    if (!adapter_ || !adapter_->retainTransfer(event.lease_id, model_->residentBytes())) {
        model_->clear();
        fail(tr("The value preview exceeds the available memory budget."));
        return;
    }
    lease_ = event.lease_id;
    hasChunk_ = true;
    loading_ = false;
    total_ = event.total_bytes;
    nextOffset_ = model_->nextOffset();
    if (!event.chunk_bytes.empty())
        windowBytes_ = event.chunk_bytes.size();
    status_->setText(
        tr("Bytes %1–%2 of %3 · %4")
            .arg(offset_)
            .arg(nextOffset_)
            .arg(total_)
            .arg(chunkKind == "binary" ? tr("Hexadecimal") : tr("Escaped UTF-8 text")));
    table_->scrollToTop();
    table_->setColumnWidth(1, 500);
    sizeVisibleColumns();
    updateActions();
}
void ValueDetailDialog::updateActions() {
    previous_->setEnabled(query_.has_value() && !loading_ && offset_ > 0);
    next_->setEnabled(query_.has_value() && !loading_ && hasChunk_ && nextOffset_ > offset_ &&
                      nextOffset_ < total_);
}
} // namespace choscordb
