#include "app/object_explorer.h"
#include "app/main_window.h"
#include "app/object_data_workspace.h"
#include "bridge/engine_adapter.h"
#include "bridge/template_service.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/menu/menu.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include "design_system/toast_region/toast_region.h"
#include <QAction>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QSyntaxHighlighter>
#include <QTabBar>
#include <QTableView>
#include <QVBoxLayout>
#include <atomic>
namespace choscordb {
namespace {
constexpr int objectIconRole = Qt::UserRole + 1;
class DdlHighlighter final : public QSyntaxHighlighter {
  public:
    explicit DdlHighlighter(QPlainTextEdit* editor)
        : QSyntaxHighlighter(editor->document()), editor_(editor) {}

  protected:
    void highlightBlock(const QString& text) override {
        const auto colors = design::resolvedThemeForWidget(*editor_).colors;
        QTextCharFormat keyword;
        keyword.setForeground(colors.accent);
        keyword.setFontWeight(QFont::DemiBold);
        static const QRegularExpression words(
            R"(\b(?:CREATE|ALTER|DROP|TABLE|VIEW|INDEX|PRIMARY|FOREIGN|KEY|REFERENCES|CONSTRAINT|NOT|NULL|DEFAULT|UNIQUE|CHECK|ON|AS|SELECT|FROM|WHERE|INSERT|INTO|UPDATE|DELETE|BOOLEAN|INTEGER|BIGINT|TEXT|TIMESTAMP|TRUE|FALSE)\b)",
            QRegularExpression::CaseInsensitiveOption);
        QVector<bool> protectedText(text.size(), false);
        QTextCharFormat literal;
        literal.setForeground(colors.action);
        QTextCharFormat identifier;
        identifier.setForeground(colors.text);
        QTextCharFormat comment;
        comment.setForeground(colors.mutedText);
        enum { Normal, String, Identifier, Backtick, BlockComment };
        int state = previousBlockState();
        if (state < Normal || state > BlockComment)
            state = Normal;
        for (int pos = 0; pos < text.size();) {
            if (state == Normal && text.mid(pos, 2) == "--") {
                setFormat(pos, text.size() - pos, comment);
                break;
            }
            if (state == Normal && text.mid(pos, 2) == "/*")
                state = BlockComment;
            const QChar quote = text.at(pos);
            bool openedHere = false;
            if (state == Normal) {
                if (quote == QLatin1Char('\'')) {
                    state = String;
                    openedHere = true;
                } else if (quote == QLatin1Char('"')) {
                    state = Identifier;
                    openedHere = true;
                } else if (quote == QLatin1Char('`')) {
                    state = Backtick;
                    openedHere = true;
                }
            }
            if (state == Normal) {
                ++pos;
                continue;
            }
            const int start = pos;
            const int segmentState = state;
            const QChar terminator = state == String       ? QLatin1Char('\'')
                                     : state == Identifier ? QLatin1Char('"')
                                                           : QLatin1Char('`');
            if (openedHere)
                ++pos;
            while (pos < text.size()) {
                if (state == BlockComment && text.mid(pos, 2) == "*/") {
                    pos += 2;
                    state = Normal;
                    break;
                }
                if (state == BlockComment) {
                    ++pos;
                    continue;
                }
                if (text.at(pos++) != terminator)
                    continue;
                if (pos < text.size() && text.at(pos) == terminator) {
                    ++pos;
                    continue;
                }
                state = Normal;
                break;
            }
            for (int index = start; index < pos; ++index)
                protectedText[index] = true;
            setFormat(start, pos - start,
                      segmentState == BlockComment ? comment
                      : segmentState == String     ? literal
                                                   : identifier);
        }
        setCurrentBlockState(state);
        auto matches = words.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            if (!protectedText[match.capturedStart()])
                setFormat(match.capturedStart(), match.capturedLength(), keyword);
        }
    }

  private:
    QPlainTextEdit* editor_;
};
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
    new DdlHighlighter(ddl_);
    pages_->addWidget(ddl_);
    pages_->addWidget(new QWidget(pages_));
    layout->addWidget(pages_, 1);
    auto* headerBody = new QWidget(this);
    headerBody->setObjectName("objectHeader");
    auto* header = new QHBoxLayout(headerBody);
    const auto metrics = design::resolveMetrics(design::Density::Compact, true);
    header->setContentsMargins(metrics.spacingMedium, metrics.spacingSmall, metrics.spacingMedium,
                               metrics.spacingSmall);
    layout->insertWidget(0, headerBody);
    auto* footerBody = new QWidget(this);
    footerBody->setObjectName("objectFooter");
    footerBody->setProperty("designSurface", "subtle");
    footerBody->setAttribute(Qt::WA_StyledBackground);
    auto* footer = new QHBoxLayout(footerBody);
    footer_ = footer;
    footer->setContentsMargins(metrics.spacingMedium, metrics.spacingSmall, metrics.spacingMedium,
                               metrics.spacingSmall);
    status_ = new design::Text(tr("Select a table or view in the sidebar."), this);
    status_->setObjectName("objectStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    footer->addWidget(status_, 1);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        QMenu menu(this);
        menu.addActions(actions());
        design::execContextMenu(menu, mapToGlobal(position));
    });
    retry_ = new QAction(tr("Retry"), this);
    retry_->setObjectName("objectRetry");
    retry_->setEnabled(false);
    addAction(retry_);
    reconnect_ = new QAction(tr("Reconnect…"), this);
    reconnect_->setObjectName("objectReconnect");
    reconnect_->setEnabled(false);
    addAction(reconnect_);
    connect(reconnect_, &QAction::triggered, this, &ObjectExplorer::reconnectRequested);
    auto* refresh = new design::Button(tr("Refresh object"), headerBody);
    refresh->setVariant(design::ButtonVariant::Outline);
    refresh->setButtonSize(design::ButtonSize::Small);
    refresh->setDesignIcon(design::Icon::Refresh);
    refresh_ = refresh;
    refresh_->setObjectName("objectRefresh");
    refresh_->setEnabled(false);
    header->addWidget(refresh_);
    auto* open = new design::Button(tr("Open query"), headerBody);
    open->setVariant(design::ButtonVariant::Outline);
    open->setButtonSize(design::ButtonSize::Small);
    open_ = open;
    open_->setObjectName("objectOpenQuery");
    open_->hide();
    connect(open_, &QPushButton::clicked, this, [this] { generateSql("select"); });
    auto* generate = new design::Button(tr("Generate SQL"), headerBody);
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
    generate_->hide();
    header->addStretch(1);
    for (auto* button : {refresh_, open_, generate_}) {
        button->setAccessibleName(button->text());
        button->setToolTip(button->text());
        button->setText({});
        auto* iconButton = qobject_cast<design::Button*>(button);
        iconButton->setButtonSize(design::ButtonSize::IconSmall);
        iconButton->setDesignIcon(button == open_       ? design::Icon::File
                                  : button == generate_ ? design::Icon::Code
                                                        : design::Icon::Refresh);
    }
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
        emit paneChanged(index);
        if (!restoredInert_)
            requestPane();
    });
    connect(retry_, &QAction::triggered, this, &ObjectExplorer::requestPane);
    connect(
        adapter_, &EngineAdapter::eventReady, this,
        [this](const BridgeEvent& event) {
            if (event.kind != "disconnected" || connection_ != event.id)
                return;
            setDisconnected();
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
                retry_->setEnabled(true);
            });
}
void ObjectExplorer::openObject(quint64 connection, const QString& object, const QString& label,
                                const QString& kind, const QVariantList& properties) {
    if (operationBusy_) {
        setStatus("busy",
                  tr("Finish or cancel the active Data operation before changing objects."));
        return;
    }
    if (connection_ == connection && object_ == object && kind_ == kind) {
        label_ = label;
        properties_ = properties;
        activateRestoredObject();
        return;
    }
    if (!connection_ && restoredInert_ && object_ == object && kind_ == kind) {
        connection_ = connection;
        label_ = label;
        properties_ = properties;
        reconnect_->setEnabled(false);
        updateActions();
        activateRestoredObject();
        return;
    }
    connection_ = connection;
    restoredInert_ = false;
    reconnect_->setEnabled(false);
    columns_.clear();
    columnsLoaded_ = false;
    tabs_->setEnabled(true);
    object_ = object;
    label_ = label;
    kind_ = kind;
    properties_ = properties;
    updateActions();
    const bool basic = kind == "index" || kind == "sequence" || kind == "function";
    tabs_->setTabText(0, basic ? tr("Details") : tr("Columns"));
    for (int i = 1; i < 5; ++i)
        tabs_->setTabVisible(i, !basic || i == 3);
    requestToken_ = 0;
    const QSignalBlocker blocker(tabs_);
    tabs_->setCurrentIndex(0);
    activePane_ = 0;
    emit objectChanged();
    requestPane();
}
void ObjectExplorer::restoreObject(std::optional<quint64> connection, const QString& object,
                                   const QString& label, const QString& kind,
                                   const QVariantList& properties) {
    if (operationBusy_)
        return;
    connection_ = connection;
    object_ = object;
    label_ = label;
    kind_ = kind;
    properties_ = properties;
    columns_.clear();
    columnsLoaded_ = false;
    requestToken_ = 0;
    restoredInert_ = true;
    model_->clear();
    ddl_->clear();
    const bool basic = kind == "index" || kind == "sequence" || kind == "function";
    tabs_->setTabText(0, basic ? tr("Details") : tr("Columns"));
    for (int i = 1; i < 5; ++i)
        tabs_->setTabVisible(i, !basic || i == 3);
    reconnect_->setEnabled(!connection_);
    updateActions();
    updateFooter();
    setStatus(connection_ ? "restored" : "disconnected",
              connection_ ? tr("%1 · Select this tab to load fresh metadata.").arg(label_)
                          : tr("%1 · Connection unavailable. Reconnect manually.").arg(label_));
    refresh_->setEnabled(false);
    emit objectChanged();
}
void ObjectExplorer::activateRestoredObject() {
    if (!restoredInert_ || !connection_)
        return;
    restoredInert_ = false;
    requestPane();
}
void ObjectExplorer::selectPane(int index) {
    if (index < 0 || index >= tabs_->count() || !tabs_->isTabVisible(index))
        return;
    tabs_->setCurrentIndex(index);
}
void ObjectExplorer::setDisconnected() {
    connection_.reset();
    restoredInert_ = true;
    columns_.clear();
    columnsLoaded_ = false;
    requestToken_ = 0;
    model_->clear();
    ddl_->clear();
    retry_->setEnabled(false);
    reconnect_->setEnabled(true);
    refresh_->setEnabled(false);
    updateActions();
    updateFooter();
    setStatus("disconnected", tr("%1 · Connection unavailable. Reconnect manually.").arg(label_));
    emit objectChanged();
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
    retry_->setEnabled(false);
    refresh_->setEnabled(false);
    if (tabs_->currentIndex() == 0 &&
        (kind_ == "index" || kind_ == "sequence" || kind_ == "function")) {
        pages_->setCurrentIndex(0);
        auto parts = QJsonDocument::fromJson(object_.toUtf8()).array();
        QString name = label_;
        QString schema = tr("Unavailable: schema metadata was not provided");
        if (parts.size() >= 2 && parts.at(0).isString()) {
            schema = parts.at(0).toString();
            if (parts.last().isString())
                name = parts.last().toString();
        } else {
            // PostgreSQL metadata supplies a qualified display label. A quoted
            // schema may contain dots, so find its closing quote explicitly.
            if (label_.startsWith('"')) {
                int end = 1;
                while (end < label_.size()) {
                    if (label_.at(end) == '"' && end + 1 < label_.size() &&
                        label_.at(end + 1) == '"') {
                        end += 2;
                        continue;
                    }
                    if (label_.at(end) == '"')
                        break;
                    ++end;
                }
                if (end < label_.size() && label_.mid(end + 1, 1) == ".") {
                    schema = label_.mid(1, end - 1).replace("\"\"", "\"");
                    name = label_.mid(end + 2);
                }
            }
        }
        const QString title = kind_ == "index"      ? tr("Index")
                              : kind_ == "sequence" ? tr("Sequence")
                                                    : tr("Function");
        model_->setHorizontalHeaderLabels({tr("Field"), tr("Value")});
        for (const auto& pair : {qMakePair(tr("Name"), name), qMakePair(tr("Kind"), title),
                                 qMakePair(tr("Schema"), schema)}) {
            model_->appendRow({new QStandardItem(pair.first), new QStandardItem(pair.second)});
        }
        if (properties_.isEmpty())
            model_->appendRow(
                {new QStandardItem(tr("Metadata")),
                 new QStandardItem(tr("Unavailable: no additional properties were provided"))});
        for (const auto& property : properties_) {
            const auto propertyData = property.toMap();
            const auto availability = propertyData.value("availability").toString();
            const auto value =
                availability == "unsupported"
                    ? tr("Unsupported: %1").arg(propertyData.value("reason").toString())
                : availability == "unavailable"
                    ? tr("Unavailable: %1").arg(propertyData.value("reason").toString())
                    : propertyData.value("value").toString();
            model_->appendRow({new QStandardItem(propertyData.value("name").toString()),
                               new QStandardItem(value)});
        }
        table_->resizeColumnsToContents();
        refresh_->setEnabled(!operationBusy_);
        setStatus("loaded", tr("%1 · Details loaded").arg(label_));
        return;
    }
    if (tabs_->currentIndex() == 4) {
        pages_->setCurrentIndex(2);
        setStatus("ready", tr("%1 · Data").arg(label_));
        emit dataRequested(*connection_, object_, label_, kind_);
        updateFooter();
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
    if (state == "loading")
        progressToast(this)->showProgress(tr("Loading object"), text);
    else
        clearProgressToast(this);
    status_->setProperty("state", state);
    status_->setText(state == "loading" ? QString{} : text);
    status_->setAccessibleName(tr("Object status: %1").arg(text));
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
    if (dataFooter_ && tabs_->currentIndex() == 4 && state == "busy") {
        // The Data footer already carries its result origin. Keep the guard's
        // explanation visible without adding a second footer or hiding Cancel.
        if (auto* main = qobject_cast<MainWindow*>(window()))
            main->showToast(text, ToastVariant::Warning);
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
    for (auto& action : dataHeaderActions_)
        if (action)
            action->deleteLater();
    dataHeaderActions_.clear();
    if (dataFooter_) {
        footer_->removeWidget(dataFooter_);
        dataFooter_->setParent(previous);
        dataFooter_ = nullptr;
    }
    pages_->removeWidget(previous);
    pages_->insertWidget(2, widget);
    if (auto* objectData = qobject_cast<ObjectDataWorkspace*>(widget)) {
        auto* header = findChild<QWidget*>("objectHeader");
        auto* headerLayout = qobject_cast<QHBoxLayout*>(header->layout());
        auto* toolbar = objectData->toolbarWidget();
        objectData->layout()->removeWidget(toolbar);
        toolbar->setParent(header);
        auto* toolbarLayout = qobject_cast<QHBoxLayout*>(toolbar->layout());
        toolbarLayout->setContentsMargins(0, 0, 0, 0);
        delete toolbarLayout->takeAt(toolbarLayout->count() - 1);
        headerLayout->insertWidget(headerLayout->count() - 1, toolbar);
        dataHeaderActions_.append(toolbar);
        dataFooter_ = objectData->footerWidget();
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
    const bool dataVisible = dataFooter_ && tabs_->currentIndex() == 4 && connection_.has_value();
    for (const auto& action : dataHeaderActions_)
        if (action)
            action->setEnabled(dataVisible);
    if (dataFooter_)
        dataFooter_->setVisible(dataVisible);
    status_->setVisible(!dataVisible);
    refresh_->setEnabled(!operationBusy_ && connection_.has_value() && !requestToken_);
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
    const bool ready = connection_.has_value() && !operationBusy_ && kind_ != "index" &&
                       kind_ != "sequence" && kind_ != "function";
    const auto unavailable =
        !connection_     ? tr("Reconnect this object's connection to use SQL actions.")
        : operationBusy_ ? tr("Finish or cancel the active Data operation.")
                         : tr("SQL generation is unavailable for this object type.");
    open_->setEnabled(ready);
    open_->setToolTip(ready ? tr("Open a new query draft for this object.") : unavailable);
    generate_->setEnabled(ready);
    generate_->setToolTip(ready ? tr("Generate SQL without running it.") : unavailable);
    for (auto it = generationActions_.begin(); it != generationActions_.end(); ++it) {
        const bool enabled =
            ready && (it.key() == "select" || it.key() == "delete" ||
                      (columnsLoaded_ && (it.key() == "insert" || !columns_.isEmpty())));
        it.value()->setEnabled(enabled);
        it.value()->setToolTip(
            enabled ? QString{}
            : ready ? tr("Load the object's columns before generating this statement.")
                    : unavailable);
    }
}
void ObjectExplorer::generateSql(const QString& kind) {
    if (!connection_ || operationBusy_ || kind_ == "index" || kind_ == "sequence" ||
        kind_ == "function")
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
