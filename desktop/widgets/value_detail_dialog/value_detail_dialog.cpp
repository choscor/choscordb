#include "widgets/value_detail_dialog/value_detail_dialog.h"
#include "bridge/engine_adapter.h"
#include "bridge/rust_text.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include "models/value_preview_model.h"
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>

namespace choscordb {
namespace {
using bridge_detail::fromRust;
int initialValueColumnWidth() {
    return 3 * design::dimension(design::Dimension::TableColumn);
}
}
ValueDetailDialog::ValueDetailDialog(EngineAdapter* adapter, QWidget* parent)
    : DialogShell(parent), adapter_(adapter), model_(new ValuePreviewModel(this)),
      table_(new QTableView(this)) {
    setObjectName("valueDetail");
    setWindowTitle(tr("Value detail"));
    setModal(false);
    resize(design::dialogInitialSize(design::DialogSize::Detail));
    table_->setObjectName("valuePreview");
    table_->setModel(model_);
    table_->setWordWrap(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    auto* sections = createSections();
    auto* heading = new design::Text(tr("Value detail"), sections);
    heading->setTextStyle(design::TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* dismiss = sections->addDismissButton(tr("Close value detail"));
    dismiss->setObjectName("valueDismiss");
    connect(dismiss, &QPushButton::clicked, this, &ValueDetailDialog::reject);
    auto* body = sections->bodyLayout();
    body->addWidget(createDescription(
        tr("Inspect a bounded window of a large text or binary value."), sections));
    body->addWidget(table_, 1);
    statusLine_ = sections->addStatusLine();
    statusLine_->setObjectName("valueStatusLine");
    status_ = statusLine_->findChild<QLabel*>("statusMessage");
    status_->setObjectName("valueStatus");
    retry_ = sections->addAction(design::DialogActionRole::Utility, tr("Retry"));
    retry_->setObjectName("valueRetry");
    retry_->hide();
    previous_ = sections->addAction(design::DialogActionRole::Utility, tr("Previous"));
    previous_->setObjectName("valuePrevious");
    previous_->setDesignIcon(design::Icon::CaretLeft);
    next_ = sections->addAction(design::DialogActionRole::Utility, tr("Next"));
    next_->setObjectName("valueNext");
    next_->setDesignIcon(design::Icon::CaretRight);
    auto* close = sections->addAction(design::DialogActionRole::Dismiss, tr("Close"));
    close->setObjectName("valueClose");
    connect(close, &QPushButton::clicked, this, &ValueDetailDialog::reject);
    connect(previous_, &QPushButton::clicked, this, &ValueDetailDialog::previousChunk);
    connect(retry_, &QPushButton::clicked, this, [this] { request(offset_); });
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
                                  : tr("Value detail · %1").arg(type.left(128)));
    show();
    request(0);
}
QString ValueDetailDialog::openInlineValue(QByteArray bytes, const QString& type, bool binary) {
    clearValue();
    if (auto error = EngineAdapter::valueDetailError(quint64(bytes.size())); !error.isEmpty())
        return error;
    inlineBytes_ = std::move(bytes);
    inlineMode_ = true;
    inlineBinary_ = binary;
    total_ = static_cast<quint64>(inlineBytes_.size());
    setWindowTitle(type.isEmpty() ? tr("Value detail")
                                  : tr("Value detail · %1").arg(type.left(128)));
    show();
    request(0);
    return {};
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
    inlineMode_ = false;
    inlineBinary_ = false;
    inlineBytes_.clear();
    alignmentTarget_.reset();
    loading_ = false;
    dropChunk();
    offset_ = total_ = nextOffset_ = 0;
    windowBytes_ = EngineAdapter::valueChunkBytes();
    statusLine_->clearReport();
    retry_->hide();
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
                                    2 * design::spacing(design::Spacing::Three));
    const int available = table_->viewport()->width() - table_->columnWidth(0);
    table_->setColumnWidth(1, std::max(width, available));
}
void ValueDetailDialog::previousChunk() {
    if ((!query_ && !inlineMode_) || loading_)
        return;
    auto target = offset_ > windowBytes_ ? offset_ - windowBytes_ : 0;
    if (inlineMode_) {
        if (!inlineBinary_)
            target = value_text_char_start(bridge_detail::byteView(inlineBytes_), target);
        request(target);
        return;
    }
    if (target == 0) {
        request(0);
        return;
    }
    alignmentTarget_ = target;
    request(target > 3 ? target - 3 : 0, 7);
}
void ValueDetailDialog::request(quint64 offset, quint32 maxBytes) {
    if ((!query_ && !inlineMode_) || loading_)
        return;
    retry_->hide();
    dropChunk();
    offset_ = offset;
    if (inlineMode_) {
        if (offset > total_) {
            fail(tr("The inline value window is invalid."));
            return;
        }
        const auto length = static_cast<qsizetype>(std::min<quint64>(
            std::min<quint32>(maxBytes, EngineAdapter::valueChunkBytes()), total_ - offset));
        if (!model_->setChunk(inlineBytes_.mid(static_cast<qsizetype>(offset), length), offset,
                              total_, inlineBinary_)) {
            fail(tr("The inline value window is invalid."));
            return;
        }
        hasChunk_ = true;
        nextOffset_ = model_->nextOffset();
        windowBytes_ = EngineAdapter::valueChunkBytes();
        statusLine_->showInfo(
            tr("Bytes %1–%2 of %3 · %4")
                .arg(offset_)
                .arg(nextOffset_)
                .arg(total_)
                .arg(inlineBinary_ ? tr("Hexadecimal") : tr("Escaped UTF-8 text")));
        table_->scrollToTop();
        table_->setColumnWidth(1, initialValueColumnWidth());
        sizeVisibleColumns();
        updateActions();
        return;
    }
    loading_ = true;
    statusLine_->showProgress(tr("Loading bytes at offset %1…").arg(offset));
    updateActions();
    if (adapter_)
        adapter_->loadValueChunk(*query_, handle_, offset, maxBytes);
    else
        fail(tr("The connection is no longer available."));
}
void ValueDetailDialog::fail(const QString& error) {
    loading_ = false;
    alignmentTarget_.reset();
    statusLine_->showError(tr("Could not load the value: %1").arg(error));
    retry_->show();
    updateActions();
}
void ValueDetailDialog::handleEvent(const BridgeEvent& event) {
    if (!loading_ || !query_ || event.id != *query_ || event.value_handle != handle_ ||
        event.chunk_offset != offset_)
        return;
    const auto kind = fromRust(event.kind);
    if (kind == "value_chunk_failed") {
        fail(fromRust(event.error));
        return;
    }
    if (kind != "value_chunk")
        return;
    if (!event.has_lease || event.chunk_bytes.size() > EngineAdapter::valueChunkBytes()) {
        fail(tr("Invalid value chunk."));
        return;
    }
    const auto chunkKind = fromRust(event.chunk_kind);
    if (alignmentTarget_) {
        auto target = *alignmentTarget_;
        if (chunkKind == "text")
            target = value_text_boundary_after({event.chunk_bytes.data(), event.chunk_bytes.size()},
                                               event.chunk_offset, target);
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
    statusLine_->showInfo(
        tr("Bytes %1–%2 of %3 · %4")
            .arg(offset_)
            .arg(nextOffset_)
            .arg(total_)
            .arg(chunkKind == "binary" ? tr("Hexadecimal") : tr("Escaped UTF-8 text")));
    table_->scrollToTop();
    table_->setColumnWidth(1, initialValueColumnWidth());
    sizeVisibleColumns();
    updateActions();
}
void ValueDetailDialog::updateActions() {
    const bool available = query_.has_value() || inlineMode_;
    previous_->setEnabled(available && !loading_ && offset_ > 0);
    next_->setEnabled(available && !loading_ && hasChunk_ && nextOffset_ > offset_ &&
                      nextOffset_ < total_);
}
}
