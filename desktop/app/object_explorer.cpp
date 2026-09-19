#include "app/object_explorer.h"
#include "app/object_data_workspace.h"
#include "app/main_window.h"
#include "bridge/engine_adapter.h"
#include "bridge/template_service.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include <QAction>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTableView>
#include <QVBoxLayout>
#include <atomic>
namespace choscordb {
namespace {
constexpr int objectIconRole = Qt::UserRole + 1;
class ObjectColumnDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        const auto role = index.data(objectIconRole);
        if (role.isValid() && option->widget) {
            option->icon = design::themedIcon(
                static_cast<design::Icon>(role.toInt()),
                design::resolvedThemeForWidget(*option->widget).colors.mutedText, 16);
            option->features |= QStyleOptionViewItem::HasDecoration;
            option->decorationSize = QSize(16, 16);
        }
    }
};
} // namespace
ObjectExplorer::ObjectExplorer(EngineAdapter* adapter, QWidget* parent)
    : QWidget(parent), adapter_(adapter) {
    setObjectName("objectScreen");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    tabs_ = new QTabBar(this);
    tabs_->setObjectName("objectTabs");
    tabs_->setAccessibleName(tr("Object inspection panes"));
    tabs_->setExpanding(false);
    for (const auto& title : {tr("Columns"), tr("Indexes"), tr("Keys"), tr("DDL"), tr("Data")})
        tabs_->addTab(title);
    layout->addWidget(tabs_);
    pages_ = new QStackedWidget(this);
    table_ = new QTableView(pages_);
    table_->setObjectName("objectMetadata");
    table_->setAccessibleName(tr("Object metadata"));
    model_ = new QStandardItemModel(this);
    table_->setModel(model_);
    table_->setItemDelegate(new ObjectColumnDelegate(table_));
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->setFrameShape(QFrame::NoFrame);
    table_->horizontalHeader()->setResizeContentsPrecision(50);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    pages_->addWidget(table_);
    ddl_ = new QPlainTextEdit(pages_);
    ddl_->setObjectName("objectDdl");
    ddl_->setAccessibleName(tr("Object DDL"));
    ddl_->setReadOnly(true);
    pages_->addWidget(ddl_);
    pages_->addWidget(new QWidget(pages_));
    layout->addWidget(pages_, 1);
    auto* footerBody = new QWidget(this);
    footerBody->setObjectName("objectFooter");
    footerBody->setProperty("designSurface", "subtle");
    footerBody->setAttribute(Qt::WA_StyledBackground);
    auto* footer = new QHBoxLayout(footerBody);
    footer_ = footer;
    const auto metrics = design::resolveMetrics(design::Density::Compact, true);
    footer->setContentsMargins(metrics.spacingMedium, metrics.spacingSmall, metrics.spacingMedium,
                               metrics.spacingSmall);
    status_ = new design::Text(tr("Select a table or view in the sidebar."), this);
    status_->setObjectName("objectStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    footer->addWidget(status_, 1);
    retry_ = new design::Button(tr("Retry"), this);
    retry_->setObjectName("objectRetry");
    retry_->hide();
    footer->addWidget(retry_);
    auto* refresh = new design::Button(tr("Refresh"), this);
    refresh->setVariant(design::ButtonVariant::Outline);
    refresh->setButtonSize(design::ButtonSize::Small);
    refresh->setDesignIcon(design::Icon::Refresh);
    refresh_ = refresh;
    refresh_->setObjectName("objectRefresh");
    refresh_->setEnabled(false);
    footer->addWidget(refresh_);
    auto* open = new design::Button(tr("Open query"), this);
    open->setVariant(design::ButtonVariant::Outline);
    open->setButtonSize(design::ButtonSize::Small);
    open_ = open;
    open_->setObjectName("objectOpenQuery");
    footer->addWidget(open_);
    connect(open_, &QPushButton::clicked, this, [this] { generateSql("select"); });
    auto* generate = new design::Button(tr("Generate SQL"), this);
    generate->setButtonSize(design::ButtonSize::Small);
    generate_ = generate;
    generate_->setObjectName("objectGenerateSql");
    auto* menu = new QMenu(generate_);
    generate_->setMenu(menu);
    for (const auto& kind :
         {QString("select"), QString("insert"), QString("update"), QString("delete")}) {
        auto* action = menu->addAction(kind.toUpper());
        action->setObjectName("objectGenerate_" + kind);
        generationActions_.insert(kind, action);
        connect(action, &QAction::triggered, this, [this, kind] { generateSql(kind); });
    }
    footer->addWidget(generate_);
    updateActions();
    connect(refresh_, &QPushButton::clicked, this, &ObjectExplorer::requestPane);
    layout->addWidget(footerBody);
    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (operationBusy_ && index != activePane_) {
            const QSignalBlocker blocker(tabs_);
            tabs_->setCurrentIndex(activePane_);
            setStatus("busy",
                      tr("Finish or cancel the active Data operation before changing panes."));
            return;
        }
        activePane_ = index;
        updateFooter();
        requestPane();
    });
    connect(retry_, &QPushButton::clicked, this, &ObjectExplorer::requestPane);
    connect(
        adapter_, &EngineAdapter::eventReady, this,
        [this](const BridgeEvent& event) {
            if (event.kind != "disconnected" || connection_ != event.id)
                return;
            connection_.reset();
            columns_.clear();
            columnsLoaded_ = false;
            updateActions();
            requestToken_ = 0;
            model_->clear();
            ddl_->clear();
            retry_->hide();
            refresh_->setEnabled(false);
            tabs_->setEnabled(false);
            setStatus("disconnected", tr("%1 · Disconnected").arg(label_));
            emit objectChanged();
        },
        Qt::DirectConnection);
    connect(adapter_, &EngineAdapter::objectInspectionReady, this,
            [this](quint64 connection, const QString& object, quint64 token,
                   const ObjectInspection& inspection) {
                if (connection_ != connection || object_ != object || !requestToken_ ||
                    requestToken_ != token)
                    return;
                requestToken_ = 0;
                refresh_->setEnabled(!operationBusy_);
                render(inspection);
            });
    connect(adapter_, &EngineAdapter::objectInspectionFailed, this,
            [this](quint64 connection, const QString& object, quint64 token, const QString& error) {
                if (connection_ != connection || object_ != object || !requestToken_ ||
                    requestToken_ != token)
                    return;
                requestToken_ = 0;
                model_->clear();
                setStatus("failed", tr("Metadata failed: %1").arg(error));
                refresh_->setEnabled(!operationBusy_);
                retry_->show();
            });
}
void ObjectExplorer::openObject(quint64 connection, const QString& object, const QString& label) {
    if (operationBusy_) {
        setStatus("busy",
                  tr("Finish or cancel the active Data operation before changing objects."));
        return;
    }
    if (connection_ == connection && object_ == object) {
        label_ = label;
        tabs_->setCurrentIndex(0);
        return;
    }
    connection_ = connection;
    columns_.clear();
    columnsLoaded_ = false;
    updateActions();
    tabs_->setEnabled(true);
    object_ = object;
    label_ = label;
    requestToken_ = 0;
    const QSignalBlocker blocker(tabs_);
    tabs_->setCurrentIndex(0);
    activePane_ = 0;
    emit objectChanged();
    requestPane();
}
void ObjectExplorer::requestPane() {
    if (!connection_ || operationBusy_)
        return;
    requestToken_ = 0;
    if (tabs_->currentIndex() == 0) {
        columns_.clear();
        columnsLoaded_ = false;
        updateActions();
    }
    model_->clear();
    ddl_->clear();
    retry_->hide();
    refresh_->setVisible(tabs_->currentIndex() != 4);
    refresh_->setEnabled(false);
    if (tabs_->currentIndex() == 4) {
        pages_->setCurrentIndex(2);
        setStatus("ready", tr("%1 · Data").arg(label_));
        emit dataRequested(*connection_, object_, label_);
        return;
    }
    pages_->setCurrentIndex(tabs_->currentIndex() == 3 ? 1 : 0);
    static std::atomic<quint64> next{quint64(1) << 53};
    requestToken_ = next.fetch_add(1);
    setStatus("loading",
              tr("%1 · Loading %2…").arg(label_, tabs_->tabText(tabs_->currentIndex()).toLower()));
    adapter_->loadObjectInspection(*connection_, object_,
                                   static_cast<ObjectInspectionPane>(tabs_->currentIndex()),
                                   requestToken_);
}
void ObjectExplorer::setStatus(const QString& state, const QString& text) {
    status_->setProperty("state", state);
    status_->setText(text);
    status_->setAccessibleName(tr("Object status: %1").arg(text));
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
    if (dataFooter_ && tabs_->currentIndex() == 4 && state == "busy") {
        // The Data footer already carries its result origin. Keep the guard's
        // explanation visible without adding a second footer or hiding Cancel.
        if (auto* main = qobject_cast<MainWindow*>(window()))
            main->showNotice(text);
        else
            status_->show();
    }
}
void ObjectExplorer::render(const ObjectInspection& inspection) {
    if (inspection.availability != MetadataAvailability::Available) {
        setStatus(inspection.availability == MetadataAvailability::Unsupported ? "unsupported"
                                                                               : "unavailable",
                  inspection.availability == MetadataAvailability::Unsupported
                      ? tr("Unsupported: %1").arg(inspection.reason)
                      : tr("Unavailable: %1").arg(inspection.reason));
        return;
    }
    if (inspection.pane == ObjectInspectionPane::Ddl) {
        ddl_->setPlainText(inspection.ddl);
        setStatus("loaded", tr("%1 · DDL loaded").arg(label_));
        return;
    }
    const auto metrics = design::resolveMetrics(design::Density::Compact, true);
    table_->verticalHeader()->setDefaultSectionSize(inspection.pane == ObjectInspectionPane::Columns
                                                        ? metrics.objectColumnRowHeight
                                                        : metrics.dataRowHeight);
    if (inspection.pane == ObjectInspectionPane::Columns) {
        const auto limits = SqlTemplateService::limits();
        quint64 remaining = limits.maxBytes;
        columnsLoaded_ = quint64(inspection.rows.size()) <= limits.maxColumns;
        for (const auto& row : inspection.rows) {
            if (!columnsLoaded_ || quint64(row.name.size()) > remaining) {
                columnsLoaded_ = false;
                columns_.clear();
                break;
            }
            remaining -= quint64(row.name.size());
            columns_.append(row.name);
        }
        updateActions();
    }
    QStringList headers{tr("Name")};
    if (inspection.pane == ObjectInspectionPane::Keys)
        headers.append(tr("Kind"));
    for (const auto& row : inspection.rows)
        for (const auto& property : row.properties)
            if (!headers.contains(property.name))
                headers.append(property.name);
    model_->setHorizontalHeaderLabels(headers);
    for (const auto& row : inspection.rows) {
        QList<QStandardItem*> items;
        for (int column = 0; column < headers.size(); ++column) {
            auto* item = new QStandardItem;
            item->setEditable(false);
            items.append(item);
        }
        items[0]->setText(row.name);
        if (inspection.pane == ObjectInspectionPane::Columns) {
            const bool primary =
                std::any_of(row.properties.begin(), row.properties.end(), [](const auto& property) {
                    return property.name == "Primary key position" &&
                           property.availability == MetadataAvailability::Available &&
                           property.value.toInt() > 0;
                });
            const auto icon = primary ? design::Icon::Key : design::Icon::File;
            items[0]->setData(static_cast<int>(icon), objectIconRole);
            items[0]->setIcon(design::themedIcon(
                icon, design::resolvedThemeForWidget(*this).colors.mutedText, 16));
            for (int column = 1; column < headers.size(); ++column)
                if (headers.at(column) == "Type" || headers.at(column) == "Default")
                    items[column]->setFont(
                        design::resolveTypography(design::TypographyRole::Metadata));
        }
        if (inspection.pane == ObjectInspectionPane::Keys)
            items[1]->setText(row.kind == "primarykey"   ? tr("Primary key")
                              : row.kind == "foreignkey" ? tr("Foreign key")
                              : row.kind == "uniquekey"  ? tr("Unique key")
                                                         : row.kind);
        for (const auto& property : row.properties) {
            const auto value = property.availability == MetadataAvailability::Available
                                   ? property.value
                               : property.availability == MetadataAvailability::Unsupported
                                   ? tr("Unsupported: %1").arg(property.reason)
                                   : tr("Unavailable: %1").arg(property.reason);
            auto* item = items[headers.indexOf(property.name)];
            item->setText(value);
            item->setToolTip(value);
        }
        model_->appendRow(items);
    }
    table_->resizeColumnsToContents();
    for (int column = 0; column < model_->columnCount(); ++column) {
        table_->setColumnWidth(column, qBound(80, table_->columnWidth(column), 320));
        model_->horizontalHeaderItem(column)->setToolTip(headers.at(column));
    }
    setStatus(
        inspection.rows.isEmpty() ? "empty" : "loaded",
        inspection.rows.isEmpty()
            ? tr("No %1 for this object.").arg(tabs_->tabText(tabs_->currentIndex()).toLower())
            : tr("%1 · %2 rows").arg(label_).arg(inspection.rows.size()));
}
void ObjectExplorer::installDataWidget(QWidget* widget) {
    auto* previous = pages_->widget(2);
    if (dataFooter_) {
        footer_->removeWidget(dataFooter_);
        dataFooter_->setParent(previous);
        dataFooter_ = nullptr;
    }
    pages_->removeWidget(previous);
    pages_->insertWidget(2, widget);
    if (auto* data = qobject_cast<ObjectDataWorkspace*>(widget)) {
        dataFooter_ = data->footerWidget();
        if (dataFooter_) {
            widget->layout()->removeWidget(dataFooter_);
            dataFooter_->setParent(footer_->parentWidget());
            dataFooter_->layout()->setContentsMargins(0, 0, 0, 0);
            footer_->insertWidget(0, dataFooter_, 1);
        }
    }
    updateFooter();
    previous->deleteLater();
}
void ObjectExplorer::updateFooter() {
    const bool data = dataFooter_ && tabs_->currentIndex() == 4;
    if (dataFooter_)
        dataFooter_->setVisible(data);
    status_->setVisible(!data);
    refresh_->setVisible(!data);
}
void ObjectExplorer::setOperationBusy(bool busy) {
    operationBusy_ = busy;
    updateActions();
    refresh_->setEnabled(!busy && connection_.has_value() && !requestToken_);
    if (busy)
        activePane_ = tabs_->currentIndex();
    else if (status_->property("state") == "busy")
        setStatus("ready", tr("%1 · %2").arg(label_, tabs_->tabText(tabs_->currentIndex())));
}
void ObjectExplorer::updateActions() {
    const bool ready = connection_.has_value() && !operationBusy_;
    open_->setEnabled(ready);
    generate_->setEnabled(ready);
    for (auto it = generationActions_.begin(); it != generationActions_.end(); ++it)
        it.value()->setEnabled(ready &&
                               (it.key() == "select" || it.key() == "delete" ||
                                (columnsLoaded_ && (it.key() == "insert" || !columns_.isEmpty()))));
}
void ObjectExplorer::generateSql(const QString& kind) {
    if (!connection_ || operationBusy_)
        return;
    if ((kind == "insert" || kind == "update") && !columnsLoaded_) {
        setStatus("unavailable", tr("Load the object's columns before generating this statement."));
        return;
    }
    const auto result = SqlTemplateService::generate(
        kind, label_, (kind == "insert" || kind == "update") ? columns_ : QStringList{});
    if (!result.valid) {
        setStatus("failed", result.error);
        return;
    }
    emit sqlGenerated(*connection_, result.sql);
}
} // namespace choscordb
