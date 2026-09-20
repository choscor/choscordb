#include "app/object_data_workspace.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "design_system/button/button.h"
#include "design_system/theme.h"
#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>
namespace choscordb {
ObjectDataWorkspace::ObjectDataWorkspace(QueryWorkspace* sqlWorkspace, QWidget* parent)
    : QWidget(parent), sql_(sqlWorkspace) {
    setObjectName("objectDataWorkspace");
    const auto metrics = design::resolveMetrics(design::Density::Compact, true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics.spacingSmall);
    auto* summary = new QLabel(tr("Open Data to read an object."), this);
    summary->setObjectName("objectDataSummary");
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(false);
    summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    summary->setMinimumWidth(0);
    auto* table = new QTableView(this);
    table->setObjectName("objectDataResults");
    table->setAccessibleName(tr("Object data"));
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setWordWrap(false);
    table->setFrameShape(QFrame::NoFrame);
    table->verticalHeader()->setDefaultSectionSize(metrics.objectDataRowHeight);
    table->horizontalHeader()->setFixedHeight(metrics.sqlResultHeaderHeight);
    table->horizontalHeader()->setResizeContentsPrecision(64);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table, 1);
    auto* messages = new QPlainTextEdit(this);
    messages->setObjectName("objectDataMessages");
    messages->setAccessibleName(tr("Object data diagnostics"));
    messages->setReadOnly(true);
    messages->setMaximumBlockCount(1000);
    messages->setMaximumHeight(metrics.dataRowHeight * 3);
    messages->hide();
    layout->addWidget(messages);
    footer_ = new QWidget(this);
    footer_->setObjectName("objectDataFooter");
    footer_->setProperty("resultFooter", true);
    auto* footer = new QHBoxLayout(footer_);
    footer->setContentsMargins(metrics.spacingMedium, metrics.spacingSmall, metrics.spacingMedium,
                               metrics.spacingSmall);
    auto makeButton = [this, footer](const QString& text, const char* name) {
        auto* button = new design::Button(text, footer_);
        button->setObjectName(name);
        button->setVariant(design::ButtonVariant::Outline);
        button->setButtonSize(design::ButtonSize::Small);
        footer->addWidget(button);
        return button;
    };
    footer->addWidget(summary, 1);
    auto* previous = makeButton({}, "objectDataPrevious");
    previous->setAccessibleName(tr("Previous page"));
    previous->setToolTip(tr("Previous page"));
    previous->setDesignIcon(design::Icon::ChevronLeft);
    previous->setButtonSize(design::ButtonSize::IconSmall);
    auto* next = makeButton({}, "objectDataNext");
    next->setAccessibleName(tr("Next page"));
    next->setToolTip(tr("Next page"));
    next->setDesignIcon(design::Icon::ChevronRight);
    next->setButtonSize(design::ButtonSize::IconSmall);
    auto* exportButton = makeButton(tr("Export…"), "objectDataExport");
    auto* addRow = makeButton(tr("Add row"), "objectDataAddRow");
    auto* deleteRows = makeButton(tr("Delete selected"), "objectDataDeleteRows");
    auto* restoreRows = makeButton(tr("Restore selected"), "objectDataRestoreRows");
    auto* setNull = makeButton(tr("Set NULL"), "objectDataSetNull");
    auto* applyEdits = makeButton(tr("Apply…"), "objectDataApply");
    auto* discardEdits = makeButton(tr("Discard"), "objectDataDiscard");
    auto* refresh = makeButton({}, "objectDataRefresh");
    refresh_ = refresh;
    refresh->setAccessibleName(tr("Refresh object data"));
    refresh->setToolTip(tr("Refresh object data"));
    refresh->setDesignIcon(design::Icon::Refresh);
    refresh->setButtonSize(design::ButtonSize::IconSmall);
    auto* cancelButton = makeButton(tr("Cancel"), "objectDataCancel");
    layout->addWidget(footer_);
    // Reuse the production result lifecycle on the same engine. Object mode has
    // no SQL document and never enables execution/transaction/profile commands.
    auto* connections = new QComboBox(this);
    auto* mode = new QComboBox(this);
    connections->hide();
    mode->hide();
    auto* run = new QAction(this);
    auto* cancel = new QAction(tr("Cancel"), this);
    auto* commit = new QAction(this);
    auto* rollback = new QAction(this);
    auto* newConnection = new QAction(this);
    result_ = new QueryWorkspace(
        {connections,
         mode,
         run,
         cancel,
         commit,
         rollback,
         newConnection,
         next,
         summary,
         messages,
         table,
         [] { return nullptr; },
         this,
         previous,
         exportButton,
         {},
         sql_->adapter(),
         true,
         addRow,
         deleteRows,
         setNull,
         applyEdits,
         discardEdits,
         [this](quint64 connection) { return sql_ && sql_->activeManualTransaction(connection); },
         restoreRows},
        this);
    connect(cancel, &QAction::changed, cancelButton, [cancel, cancelButton] {
        cancelButton->setEnabled(cancel->isEnabled());
        cancelButton->setText(cancel->text());
    });
    cancelButton->setEnabled(cancel->isEnabled());
    cancelButton->hide();
    connect(cancelButton, &QPushButton::clicked, cancel, &QAction::trigger);
    connect(refresh_, &QPushButton::clicked, this,
            [this] { openObject(connection_, object_, label_, kind_); });
    refresh_->setEnabled(false);
    connect(result_, &QueryWorkspace::executionStateChanged, messages,
            [messages](const QString& state) { messages->setVisible(state == "failed"); });
    connect(result_, &QueryWorkspace::activityChanged, this, [this, cancelButton](bool busy) {
        if (sql_)
            sql_->setExternalWork(busy);
        refresh_->setEnabled(!busy && !object_.isEmpty());
        cancelButton->setProperty("busy", busy);
        cancelButton->setVisible(busy);
        emit busyChanged(busy);
    });
    connect(sql_, &QueryWorkspace::activityChanged, this,
            [this](bool busy) { refresh_->setEnabled(!busy && !object_.isEmpty()); });
    connect(sql_, &QueryWorkspace::transactionStateChanged, this,
            [this](quint64, bool) { result_->refreshEditActions(); });
}
void ObjectDataWorkspace::openObject(quint64 connection, const QString& object,
                                     const QString& label, const QString& kind) {
    if (!sql_ || !sql_->navigationAllowed() || !result_->navigationAllowed() || object.isEmpty())
        return;
    connection_ = connection;
    object_ = object;
    label_ = label;
    kind_ = kind;
    result_->openObjectData(connection, object, label, sql_->queryPreferences(), kind);
}
void ObjectDataWorkspace::invalidate() {
    if (!resolvePendingEdits())
        return;
    object_.clear();
    label_.clear();
    kind_.clear();
    refresh_->setEnabled(false);
    result_->invalidateResult();
}
bool ObjectDataWorkspace::resolvePendingEdits() {
    return !result_ || result_->resolvePendingEdits();
}
} // namespace choscordb
