#include "app/object_data_workspace.h"
#include "app/query_workspace.h"
#include "app/result_column_header.h"
#include "bridge/engine_adapter.h"
#include "design_system/button/button.h"
#include "design_system/status_line/status_line.h"
#include "design_system/table/table_style.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QTableView>
#include <QVBoxLayout>
#include <memory>
#include <utility>
namespace choscordb {
namespace {
struct HeaderSizingState {
    QStringList labels;
    QVector<bool> keys;
    bool newLabels = false;
};
} // namespace
ObjectDataWorkspace::ObjectDataWorkspace(QueryWorkspace* sqlWorkspace, QWidget* parent)
    : QWidget(parent), sql_(sqlWorkspace) {
    setObjectName("objectDataWorkspace");
    setProperty("designSurface", "panel");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(design::spacing(design::Spacing::One));
    auto* summary = new design::Text(tr("Open Data to read an object."), this);
    summary->setObjectName("objectDataSummary");
    auto* table = new QTableView(this);
    table->setObjectName("objectDataResults");
    table->setHorizontalHeader(new ResultColumnHeader(table));
    table->setAccessibleName(tr("Object data"));
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    design::configureResultTable(*table, false);
    table->setWordWrap(false);
    table->setFrameShape(QFrame::NoFrame);
    table->verticalHeader()->setDefaultSectionSize(design::dimension(design::Dimension::Row));
    table->horizontalHeader()->setFixedHeight(design::dimension(design::Dimension::Header));
    table->horizontalHeader()->setDefaultSectionSize(
        design::dimension(design::Dimension::TableColumn));
    table->horizontalHeader()->setStretchLastSection(false);
    layout->addWidget(table, 1);
    auto* messages = new QPlainTextEdit(this);
    messages->setObjectName("objectDataMessages");
    messages->setAccessibleName(tr("Object data diagnostics"));
    messages->setReadOnly(true);
    messages->setMaximumBlockCount(1000);
    messages->setMaximumHeight(design::dimension(design::Dimension::Row) * 3);
    messages->hide();
    layout->addWidget(messages);
    auto* statusLine = new design::StatusLine(this);
    footer_ = statusLine;
    footer_->setObjectName("objectDataFooter");
    toolbar_ = new QWidget(this);
    toolbar_->setObjectName("objectDataToolbar");
    auto* toolbar = new QHBoxLayout(toolbar_);
    toolbar->setContentsMargins(
        design::spacing(design::Spacing::Two), design::spacing(design::Spacing::One),
        design::spacing(design::Spacing::Two), design::spacing(design::Spacing::One));
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
    const auto metric = [statusLine](const char* name) {
        auto* text = new design::Text({}, statusLine);
        text->setObjectName(QString::fromLatin1(name));
        return text;
    };
    auto* outcome = metric("objectDataOutcome");
    auto* duration = metric("objectDataDuration");
    auto* memory = metric("objectDataVisibleSize");
    auto* page = metric("objectDataPage");
    auto* rows = metric("objectDataRows");
    auto* previous = new design::Button({}, statusLine);
    previous->setObjectName("objectDataPrevious");
    previous->setAccessibleName(tr("Previous page"));
    previous->setToolTip(tr("Previous page"));
    auto* next = new design::Button({}, statusLine);
    next->setObjectName("objectDataNext");
    next->setAccessibleName(tr("Next page"));
    next->setToolTip(tr("Next page"));
    statusLine->configure({summary, outcome, duration, memory, page, rows, previous, next});
    statusLine->setContent({{}, tr("Open Data to read an object."), {}, {}, {}, {}},
                           design::StatusLine::State::Neutral);
    // Shown only for relations that keep history; Rust validates the time.
    history_ = new QWidget(toolbar_);
    history_->setObjectName("objectDataHistory");
    auto* historyLayout = new QHBoxLayout(history_);
    historyLayout->setContentsMargins(0, 0, 0, 0);
    historyLayout->setSpacing(design::spacing(design::Spacing::One));
    timeMode_ = new QComboBox(history_);
    timeMode_->setObjectName("objectDataTimeMode");
    timeMode_->setAccessibleName(tr("Rows to show"));
    timeMode_->setToolTip(tr("Rows to show"));
    timeMode_->addItem(tr("Current"), int(ObjectDataTime::Mode::Current));
    timeMode_->addItem(tr("As of…"), int(ObjectDataTime::Mode::AsOf));
    timeMode_->addItem(tr("All history"), int(ObjectDataTime::Mode::All));
    asOf_ = new QLineEdit(history_);
    asOf_->setObjectName("objectDataAsOf");
    asOf_->setAccessibleName(tr("History time"));
    asOf_->setPlaceholderText(tr("YYYY-MM-DD HH:MM:SS"));
    asOf_->hide();
    historyLayout->addWidget(timeMode_);
    historyLayout->addWidget(asOf_);
    toolbar->addWidget(history_);
    history_->hide();
    auto* exportButton =
        makeButton(toolbar, tr("Export…"), "objectDataExport", design::Icon::Export);
    auto* addRow = makeButton(toolbar, tr("Add row"), "objectDataAddRow", design::Icon::Add);
    auto* deleteRows =
        makeButton(toolbar, tr("Delete selected"), "objectDataDeleteRows", design::Icon::Close);
    auto* restoreRows =
        makeButton(toolbar, tr("Restore selected"), "objectDataRestoreRows", design::Icon::Refresh);
    auto* setNull = makeButton(toolbar, tr("Set NULL"), "objectDataSetNull", design::Icon::Square);
    auto* discardEdits =
        makeButton(toolbar, tr("Discard"), "objectDataDiscardEdits", design::Icon::Cancel);
    // Keep the shared edit controls as command/state bindings for the table menu.
    for (auto* button : {addRow, deleteRows, restoreRows, setNull, discardEdits}) {
        toolbar->removeWidget(button);
        button->hide();
    }
    auto* applyEdits = makeButton(toolbar, tr("Apply…"), "objectDataApply", design::Icon::Check);
    auto* cancelButton =
        makeButton(toolbar, tr("Cancel"), "objectDataCancel", design::Icon::Cancel);
    toolbar->removeWidget(cancelButton);
    cancelButton->hide();
    toolbar->addStretch(1);
    layout->addWidget(footer_);
    // Reuse the production result lifecycle on the same engine. Object mode has
    // no SQL document and never enables execution/transaction/profile commands.
    auto* run = new QAction(this);
    auto* runAll = new QAction(this);
    auto* cancel = new QAction(tr("Cancel"), this);
    auto* newConnection = new QAction(this);
    result_ = new QueryWorkspace(
        {run,
         runAll,
         cancel,
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
         [this](quint64 connection) { return sql_ && sql_->activeTransaction(connection); },
         restoreRows,
         outcome,
         duration,
         page,
         rows,
         memory},
        this);
    auto headerSizing = std::make_shared<HeaderSizingState>();
    connect(table->model(), &QAbstractItemModel::modelReset, table, [table, headerSizing] {
        auto* model = table->model();
        QStringList current;
        for (int column = 0; column < model->columnCount(); ++column)
            current.append(model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString());
        headerSizing->newLabels = current != headerSizing->labels;
        if (!headerSizing->newLabels)
            return;
        headerSizing->labels = current;
        auto* header = table->horizontalHeader();
        for (int column = 0; column < current.size(); ++column)
            header->resizeSection(column, header->sectionSizeHint(column));
    });
    connect(table->model(), &QAbstractItemModel::headerDataChanged, table,
            [table, headerSizing](Qt::Orientation orientation, int first, int last) {
                if (orientation != Qt::Horizontal)
                    return;
                auto* model = table->model();
                auto* header = table->horizontalHeader();
                headerSizing->keys.resize(model->columnCount());
                for (int column = first; column <= last; ++column) {
                    const bool key =
                        model->headerData(column, Qt::Horizontal, ResultTableModel::HeaderKeyRole)
                            .toBool();
                    if ((headerSizing->newLabels || key != headerSizing->keys[column]) && key)
                        header->resizeSection(column, qMax(header->sectionSize(column),
                                                           header->sectionSizeHint(column)));
                    headerSizing->keys[column] = key;
                }
                headerSizing->newLabels = false;
            });
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
            sql_->setExternalWork(this, busy);
        cancelButton->setProperty("busy", busy);
        emit busyChanged(busy);
    });
    connect(result_, &QueryWorkspace::foreignKeyRequested, this,
            &ObjectDataWorkspace::foreignKeyRequested);
    connect(result_, &QueryWorkspace::activityChanged, history_,
            [this](bool busy) { history_->setEnabled(!busy); });
    connect(timeMode_, &QComboBox::currentIndexChanged, this, [this] {
        const bool asOf = time().mode == ObjectDataTime::Mode::AsOf;
        asOf_->setVisible(asOf);
        if (asOf) {
            asOf_->setFocus();
            return;
        }
        readSelectedTime();
    });
    connect(asOf_, &QLineEdit::returnPressed, this, &ObjectDataWorkspace::readSelectedTime);
    connect(sql_, &QueryWorkspace::transactionStateChanged, this,
            [this](quint64, bool) { result_->refreshEditActions(); });
}
void ObjectDataWorkspace::openObject(quint64 connection, const QString& object,
                                     const QString& label, const QString& relationSubtype) {
    if (!canRead() || object.isEmpty())
        return;
    const bool history = EngineAdapter::relationSubtypeHasHistory(relationSubtype);
    if (!history || connection != connection_ || object != object_) {
        const QSignalBlocker blocker(timeMode_);
        timeMode_->setCurrentIndex(0);
        shownTimeMode_ = 0;
        asOf_->clear();
        asOf_->hide();
    }
    history_->setVisible(history);
    connection_ = connection;
    object_ = object;
    label_ = label;
    read(std::exchange(initialFilter_, {}));
}
bool ObjectDataWorkspace::canRead() const {
    return sql_ && sql_->navigationAllowed() && result_->navigationAllowed();
}
ObjectDataTime ObjectDataWorkspace::time() const {
    const auto mode = ObjectDataTime::Mode(timeMode_->currentData().toInt());
    return {mode, mode == ObjectDataTime::Mode::AsOf ? asOf_->text() : QString{}};
}
bool ObjectDataWorkspace::read(const QString& filter) {
    result_->setDriverForConnection(connection_, sql_->driverForConnection(connection_));
    return result_->openObjectData(connection_, object_, label_, sql_->queryPreferences(), false,
                                   filter, time());
}
void ObjectDataWorkspace::readSelectedTime() {
    if (canRead() && read({})) {
        shownTimeMode_ = timeMode_->currentIndex();
        return;
    }
    const QSignalBlocker blocker(timeMode_);
    timeMode_->setCurrentIndex(shownTimeMode_);
    asOf_->setVisible(time().mode == ObjectDataTime::Mode::AsOf);
}
void ObjectDataWorkspace::invalidate(bool connectionLost) {
    if (!resolvePendingEdits())
        return;
    result_->invalidateResult(connectionLost);
}
bool ObjectDataWorkspace::resolvePendingEdits() {
    return !result_ || result_->resolvePendingEdits();
}
} // namespace choscordb
