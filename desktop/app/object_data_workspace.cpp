#include "app/object_data_workspace.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "design_system/button/button.h"
#include "design_system/table/table_style.h"
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
    design::configureResultTable(*table, false);
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
    toolbar_ = new QWidget(this);
    toolbar_->setObjectName("objectDataToolbar");
    auto* toolbar = new QHBoxLayout(toolbar_);
    toolbar->setContentsMargins(metrics.spacingMedium, metrics.spacingSmall, metrics.spacingMedium,
                                metrics.spacingSmall);
    layout->insertWidget(0, toolbar_);
    auto makeButton = [](QHBoxLayout* target, const QString& label, const char* name,
                         design::Icon icon) {
        auto* button = new design::Button({}, target->parentWidget());
        button->setObjectName(name);
        button->setAccessibleName(label);
        button->setToolTip(label);
        button->setVariant(design::ButtonVariant::Outline);
        button->setButtonSize(design::ButtonSize::IconSmall);
        button->setDesignIcon(icon);
        target->addWidget(button);
        return button;
    };
    footer->addWidget(summary, 1);
    auto* previous =
        makeButton(footer, tr("Previous page"), "objectDataPrevious", design::Icon::ChevronLeft);
    auto* next = makeButton(footer, tr("Next page"), "objectDataNext", design::Icon::ChevronRight);
    auto* exportButton =
        makeButton(toolbar, tr("Export…"), "objectDataExport", design::Icon::Export);
    auto* addRow = makeButton(toolbar, tr("Add row"), "objectDataAddRow", design::Icon::Add);
    auto* deleteRows =
        makeButton(toolbar, tr("Delete selected"), "objectDataDeleteRows", design::Icon::Close);
    auto* restoreRows =
        makeButton(toolbar, tr("Restore selected"), "objectDataRestoreRows", design::Icon::Refresh);
    auto* setNull = makeButton(toolbar, tr("Set NULL"), "objectDataSetNull", design::Icon::Square);
    // Keep the shared edit controls as command/state bindings for the table menu.
    for (auto* button : {addRow, deleteRows, restoreRows, setNull}) {
        toolbar->removeWidget(button);
        button->hide();
    }
    auto* applyEdits = makeButton(toolbar, tr("Apply…"), "objectDataApply", design::Icon::Check);
    auto* cancelButton =
        makeButton(toolbar, tr("Cancel"), "objectDataCancel", design::Icon::Cancel);
    toolbar->addStretch(1);
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
         nullptr,
         [this](quint64 connection) { return sql_ && sql_->activeManualTransaction(connection); },
         restoreRows},
        this);
    connect(cancel, &QAction::changed, cancelButton, [cancel, cancelButton] {
        cancelButton->setEnabled(cancel->isEnabled());
        cancelButton->setAccessibleName(cancel->text());
        cancelButton->setToolTip(cancel->text());
    });
    cancelButton->setEnabled(cancel->isEnabled());
    connect(cancelButton, &QPushButton::clicked, cancel, &QAction::trigger);
    connect(result_, &QueryWorkspace::executionStateChanged, messages,
            [messages](const QString& state) { messages->setVisible(state == "failed"); });
    connect(result_, &QueryWorkspace::activityChanged, this, [this, cancelButton](bool busy) {
        if (sql_)
            sql_->setExternalWork(busy);
        cancelButton->setProperty("busy", busy);
        emit busyChanged(busy);
    });
    connect(sql_, &QueryWorkspace::transactionStateChanged, this,
            [this](quint64, bool) { result_->refreshEditActions(); });
}
void ObjectDataWorkspace::openObject(quint64 connection, const QString& object,
                                     const QString& label, const QString& kind) {
    if (!sql_ || !sql_->navigationAllowed() || !result_->navigationAllowed() || object.isEmpty())
        return;
    result_->openObjectData(connection, object, label, sql_->queryPreferences(), kind);
}
void ObjectDataWorkspace::invalidate() {
    if (!resolvePendingEdits())
        return;
    result_->invalidateResult();
}
bool ObjectDataWorkspace::resolvePendingEdits() {
    return !result_ || result_->resolvePendingEdits();
}
} // namespace choscordb
