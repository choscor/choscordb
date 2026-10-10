#include "app/object_explorer.h"
#include "app/main_window.h"
#include "app/object_data_workspace.h"
#include "app/object_kind_icon.h"
#include "bridge/engine_adapter.h"
#include "bridge/request_token.h"
#include "bridge/rust_text.h"
#include "bridge/sql_highlight.h"
#include "bridge/template_service.h"
#include "design_system/button/button.h"
#include "design_system/menu/menu.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include "design_system/toast_region/toast_region.h"
#include "widgets/object_erd_widget.h"
#include <QAction>
#include <QEvent>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QSyntaxHighlighter>
#include <QTabBar>
#include <QTableView>
#include <QTextBlock>
#include <QVBoxLayout>
#include <algorithm>
namespace choscordb {
namespace {
// Objects whose tab shows only details and DDL; unknown kinds keep the relation layout.
bool basicObject(const QString& kind) {
    const auto traits = EngineAdapter::objectKindTraits(kind);
    return traits.opensObjectTab && !traits.relation;
}
constexpr int objectIconRole = Qt::UserRole + 1;
class DdlEditor final : public QPlainTextEdit {
  public:
    explicit DdlEditor(QWidget* parent) : QPlainTextEdit(parent), gutter_(new QWidget(this)) {
        gutter_->setObjectName("objectDdlLineNumbers");
        gutter_->setAccessibleName(tr("DDL line numbers"));
        gutter_->installEventFilter(this);
        setLineWrapMode(QPlainTextEdit::NoWrap);
        document()->setDocumentMargin(design::spacing(design::Spacing::Two));
        connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateGutterWidth(); });
        connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
            if (dy)
                gutter_->scroll(0, dy);
            else
                gutter_->update(0, rect.y(), gutter_->width(), rect.height());
        });
        updateGutterWidth();
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == gutter_ && event->type() == QEvent::Paint) {
            paintGutter(static_cast<QPaintEvent*>(event));
            return true;
        }
        return QPlainTextEdit::eventFilter(watched, event);
    }
    void resizeEvent(QResizeEvent* event) override {
        QPlainTextEdit::resizeEvent(event);
        const auto area = contentsRect();
        gutter_->setGeometry(area.left(), area.top(), gutterWidth(), area.height());
    }
    void changeEvent(QEvent* event) override {
        QPlainTextEdit::changeEvent(event);
        if (event->type() == QEvent::FontChange)
            updateGutterWidth();
        if (event->type() == QEvent::PaletteChange)
            gutter_->update();
    }

  private:
    int gutterWidth() const {
        const int digits = qMax(3, QString::number(blockCount()).size());
        return fontMetrics().horizontalAdvance(QString(digits, QLatin1Char('0'))) + 12;
    }
    void updateGutterWidth() {
        gutter_->setFont(font());
        setViewportMargins(gutterWidth(), 0, 0, 0);
        const auto area = contentsRect();
        gutter_->setGeometry(area.left(), area.top(), gutterWidth(), area.height());
        gutter_->update();
    }
    void paintGutter(QPaintEvent* event) {
        QPainter painter(gutter_);
        painter.fillRect(event->rect(), design::resolvedThemeForWidget(*this).colors.surfaceRaised);
        painter.setPen(palette().color(QPalette::Text));
        painter.setFont(font());
        auto block = firstVisibleBlock();
        auto top = blockBoundingGeometry(block).translated(contentOffset()).top();
        while (block.isValid() && top <= event->rect().bottom()) {
            const auto height = blockBoundingRect(block).height();
            if (block.isVisible() && top + height >= event->rect().top())
                painter.drawText(QRect(0, qRound(top), gutter_->width() - 6, qRound(height)),
                                 Qt::AlignRight | Qt::AlignVCenter,
                                 QString::number(block.blockNumber() + 1));
            top += height;
            block = block.next();
        }
    }
    QWidget* gutter_;
};
class DdlHighlighter final : public QSyntaxHighlighter {
  public:
    explicit DdlHighlighter(QPlainTextEdit* editor)
        : QSyntaxHighlighter(editor->document()), editor_(editor) {
        editor_->installEventFilter(this);
        // Spans come from Rust once per text change; highlightBlock only paints them.
        connect(editor_->document(), &QTextDocument::contentsChange, this,
                [this](int, int removed, int added) {
                    if (updating_ || (!removed && !added))
                        return;
                    spans_ = sqlHighlightSpans(editor_->toPlainText());
                    updating_ = true;
                    rehighlight();
                    updating_ = false;
                });
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == editor_ && event->type() == QEvent::PaletteChange)
            rehighlight();
        return QSyntaxHighlighter::eventFilter(watched, event);
    }

    void highlightBlock(const QString& text) override {
        const auto colors = design::resolvedThemeForWidget(*editor_).colors;
        const auto base = editor_->palette().color(QPalette::Base);
        const auto foreground = editor_->palette().color(QPalette::Text);
        const auto readable = [&](const QColor& color) {
            return design::contrastRatio(color, base) >= 4.5 ? color : foreground;
        };
        QTextCharFormat keyword;
        keyword.setForeground(readable(colors.codeKeyword));
        keyword.setFontWeight(QFont::DemiBold);
        QTextCharFormat literal;
        literal.setForeground(readable(colors.codeString));
        QTextCharFormat identifier;
        identifier.setForeground(foreground);
        QTextCharFormat comment;
        comment.setForeground(readable(colors.codeComment));
        QTextCharFormat number;
        number.setForeground(readable(colors.codeNumber));
        const int blockStart = currentBlock().position();
        const int blockEnd = blockStart + static_cast<int>(text.size());
        auto span = std::lower_bound(spans_.begin(), spans_.end(), blockStart,
                                     [](const SqlHighlightSpan& candidate, int position) {
                                         return candidate.start + candidate.length <= position;
                                     });
        for (; span != spans_.end() && span->start < blockEnd; ++span) {
            const int start = std::max(span->start, blockStart);
            const int end = std::min(span->start + span->length, blockEnd);
            const auto& format = span->kind == SqlHighlight::Keyword      ? keyword
                                 : span->kind == SqlHighlight::String     ? literal
                                 : span->kind == SqlHighlight::Identifier ? identifier
                                 : span->kind == SqlHighlight::Comment    ? comment
                                                                          : number;
            setFormat(start - blockStart, end - start, format);
        }
    }

  private:
    QPlainTextEdit* editor_;
    std::vector<SqlHighlightSpan> spans_;
    bool updating_ = false;
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
                design::resolvedThemeForWidget(*option->widget).colors.fgMuted, objectIconSize());
            option->features |= QStyleOptionViewItem::HasDecoration;
            option->decorationSize = QSize(objectIconSize(), objectIconSize());
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
    for (const auto& title :
         {tr("Columns"), tr("Indexes"), tr("Keys"), tr("DDL"), tr("ERD"), tr("Data")})
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
    // resizeColumnsToContents() samples rows by the vertical header's precision.
    table_->horizontalHeader()->setResizeContentsPrecision(50);
    table_->verticalHeader()->setResizeContentsPrecision(50);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    pages_->addWidget(table_);
    ddl_ = new DdlEditor(pages_);
    ddl_->setObjectName("objectDdl");
    ddl_->setAccessibleName(tr("Object DDL"));
    ddl_->setReadOnly(true);
    ddl_->setProperty("designRole", "codePreview");
    ddl_->setFont(design::resolveTypography(design::TypographyRole::Mono));
    ddl_->setFrameShape(QFrame::NoFrame);
    new DdlHighlighter(ddl_);
    pages_->addWidget(ddl_);
    erd_ = new ObjectErdWidget(pages_);
    pages_->addWidget(erd_);
    pages_->addWidget(new QWidget(pages_));
    connect(erd_, &ObjectErdWidget::tableActivated, this,
            [this](const QString& id, const QString& qualifiedName) {
                if (connection_ && !operationBusy_)
                    emit relatedTableActivated(*connection_, id, qualifiedName);
            });
    layout->addWidget(pages_, 1);
    auto* headerBody = new QWidget(this);
    headerBody->setObjectName("objectHeader");
    auto* header = new QHBoxLayout(headerBody);
    header->setContentsMargins(
        design::spacing(design::Spacing::Two), design::spacing(design::Spacing::One),
        design::spacing(design::Spacing::Two), design::spacing(design::Spacing::One));
    layout->insertWidget(0, headerBody);
    auto* footerBody = new design::StatusLine(this);
    footerBody->setObjectName("objectFooter");
    footer_ = footerBody;
    status_ = new design::Text(tr("Select a table or view in the sidebar."), this);
    status_->setObjectName("objectStatus");
    footer_->configure({nullptr, status_});
    footer_->setContent({{}, tr("Select a table or view in the sidebar."), {}, {}, {}, {}},
                        design::StatusLine::State::Neutral);
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
    connect(refresh_, &QPushButton::clicked, this, [this] {
        if (tabs_->currentIndex() == 4)
            graphLoaded_ = false;
        requestPane();
    });
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
    connect(retry_, &QAction::triggered, this, [this] {
        if (tabs_->currentIndex() == 4)
            graphLoaded_ = false;
        requestPane();
    });
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
    connect(
        adapter_, &EngineAdapter::objectGraphReady, this,
        [this](quint64 connection, const QString& object, quint64 token, const ObjectGraph& graph) {
            if (connection_ != connection || object_ != object || !requestToken_ ||
                requestToken_ != token)
                return;
            requestToken_ = 0;
            graphLoaded_ = true;
            refresh_->setEnabled(!operationBusy_);
            renderGraph(graph);
        });
    connect(adapter_, &EngineAdapter::objectGraphFailed, this,
            [this](quint64 connection, const QString& object, quint64 token, const QString& error) {
                if (connection_ != connection || object_ != object || !requestToken_ ||
                    requestToken_ != token)
                    return;
                requestToken_ = 0;
                erd_->clearGraph();
                setStatus("failed", tr("ERD failed: %1").arg(error));
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
        updateFooter();
        activateRestoredObject();
        return;
    }
    connection_ = connection;
    restoredInert_ = false;
    reconnect_->setEnabled(false);
    columns_.clear();
    columnsLoaded_ = false;
    graphLoaded_ = false;
    erd_->clearGraph();
    tabs_->setEnabled(true);
    object_ = object;
    label_ = label;
    kind_ = kind;
    properties_ = properties;
    updateActions();
    const bool basic = basicObject(kind);
    tabs_->setTabText(0, basic ? tr("Details") : tr("Columns"));
    for (int i = 1; i < 6; ++i)
        tabs_->setTabVisible(i, (!basic || i == 3) &&
                                    (i != 4 || EngineAdapter::objectKindTraits(kind).diagram));
    requestToken_ = 0;
    const QSignalBlocker blocker(tabs_);
    tabs_->setCurrentIndex(0);
    activePane_ = 0;
    updateFooter();
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
    graphLoaded_ = false;
    erd_->clearGraph();
    requestToken_ = 0;
    restoredInert_ = true;
    model_->clear();
    ddl_->clear();
    const bool basic = basicObject(kind);
    tabs_->setTabText(0, basic ? tr("Details") : tr("Columns"));
    for (int i = 1; i < 6; ++i)
        tabs_->setTabVisible(i, (!basic || i == 3) &&
                                    (i != 4 || EngineAdapter::objectKindTraits(kind).diagram));
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
    graphLoaded_ = false;
    erd_->clearGraph();
    requestToken_ = 0;
    model_->clear();
    ddl_->clear();
    retry_->setEnabled(false);
    reconnect_->setEnabled(true);
    refresh_->setEnabled(false);
    updateActions();
    updateFooter();
    if (status_->property("state") != "failed")
        setStatus("disconnected",
                  tr("%1 · Connection unavailable. Reconnect manually.").arg(label_));
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
    if (tabs_->currentIndex() == 0 && basicObject(kind_)) {
        pages_->setCurrentIndex(0);
        const auto idBytes = object_.toUtf8();
        const auto labelBytes = label_.toUtf8();
        const auto display = object_display_identity_policy(bridge_detail::utf8View(idBytes),
                                                            bridge_detail::utf8View(labelBytes));
        const auto name = bridge_detail::fromRust(display.name);
        const auto schema = display.has_schema
                                ? bridge_detail::fromRust(display.schema)
                                : tr("Unavailable: schema metadata was not provided");
        const QString title = objectKindTitle(kind_);
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
        // perf-ok: object metadata; precision limits the sample to 50 rows.
        table_->resizeColumnsToContents();
        refresh_->setEnabled(!operationBusy_);
        setStatus("loaded", tr("%1 · Details loaded").arg(label_));
        return;
    }
    if (tabs_->currentIndex() == 5) {
        pages_->setCurrentIndex(3);
        setStatus("ready", tr("%1 · Data").arg(label_));
        emit dataRequested(*connection_, object_, label_);
        updateFooter();
        return;
    }
    if (tabs_->currentIndex() == 4) {
        pages_->setCurrentWidget(erd_);
        if (graphLoaded_) {
            setStatus(graphState_, graphMessage_);
            refresh_->setEnabled(!operationBusy_);
            return;
        }
        erd_->clearGraph();
        requestToken_ = nextRequestToken();
        setStatus("loading", tr("%1 · Loading ERD…").arg(label_));
        adapter_->loadObjectGraph(*connection_, object_, requestToken_);
        return;
    }
    pages_->setCurrentIndex(tabs_->currentIndex() == 3 ? 1 : 0);
    requestToken_ = nextRequestToken();
    setStatus("loading",
              tr("%1 · Loading %2…").arg(label_, tabs_->tabText(tabs_->currentIndex()).toLower()));
    adapter_->loadObjectInspection(*connection_, object_,
                                   static_cast<ObjectInspectionPane>(tabs_->currentIndex()),
                                   requestToken_);
}
void ObjectExplorer::setStatus(const QString& state, const QString& text) {
    auto* line = footer_;
    line->setBusy(state == "loading" || operationBusy_);
    status_->setProperty("state", state);
    const auto semantic = state == "failed" ? design::StatusLine::State::Error
                          : state == "loaded" || state == "empty"
                              ? design::StatusLine::State::Success
                              : design::StatusLine::State::Neutral;
    footer_->setContent({{}, text, {}, {}, {}, {}}, semantic);
    status_->setAccessibleName(tr("Object status: %1").arg(text));
    if (dataFooter_ && tabs_->currentIndex() == 5 && state == "busy") {
        // The Data footer already carries its result origin. Keep the guard's
        // explanation visible without adding a second footer or hiding Cancel.
        if (auto* dataLine = qobject_cast<design::StatusLine*>(dataFooter_.data()))
            dataLine->setMessage(text);
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
    table_->verticalHeader()->setDefaultSectionSize(design::dimension(design::Dimension::Row));
    if (inspection.pane == ObjectInspectionPane::Columns) {
        // Rust rejects templates beyond its column and size limits.
        columnsLoaded_ = true;
        for (const auto& row : inspection.rows)
            columns_.append(row.name);
        updateActions();
    }
    QStringList headers{tr("Name")};
    if (inspection.pane == ObjectInspectionPane::Keys)
        headers.append(tr("Kind"));
    QHash<QString, int> columnFor;
    for (int column = 0; column < headers.size(); ++column)
        columnFor.insert(headers.at(column), column);
    for (const auto& row : inspection.rows)
        for (const auto& property : row.properties)
            if (!columnFor.contains(property.name)) {
                columnFor.insert(property.name, int(headers.size()));
                headers.append(property.name);
            }
    model_->setHorizontalHeaderLabels(headers);
    // Resolve per-render presentation once rather than per row or cell.
    const auto mutedText = design::resolvedThemeForWidget(*this).colors.fgMuted;
    const QIcon keyIcon = design::themedIcon(design::Icon::Key, mutedText, objectIconSize());
    const QIcon fileIcon = design::themedIcon(design::Icon::File, mutedText, objectIconSize());
    const QFont metadataFont = design::resolveTypography(design::TypographyRole::Metadata);
    // Grow the model once, then fill cells, instead of one insertion per row.
    const int firstRow = model_->rowCount();
    model_->setRowCount(firstRow + int(inspection.rows.size()));
    int modelRow = firstRow;
    for (const auto& row : inspection.rows) {
        QList<QStandardItem*> items;
        items.reserve(headers.size());
        for (int column = 0; column < headers.size(); ++column) {
            auto* item = new QStandardItem;
            item->setEditable(false);
            items.append(item);
        }
        items[0]->setText(row.name);
        if (inspection.pane == ObjectInspectionPane::Columns) {
            const bool primary = row.primaryKey;
            const auto icon = primary ? design::Icon::Key : design::Icon::File;
            items[0]->setData(static_cast<int>(icon), objectIconRole);
            items[0]->setIcon(primary ? keyIcon : fileIcon);
            for (int column = 1; column < headers.size(); ++column)
                if (headers.at(column) == "Type" || headers.at(column) == "Default")
                    items[column]->setFont(metadataFont);
        }
        if (inspection.pane == ObjectInspectionPane::Keys)
            items[1]->setText(keyKindTitle(row.kind));
        for (const auto& property : row.properties) {
            const auto value = property.availability == MetadataAvailability::Available
                                   ? property.value
                               : property.availability == MetadataAvailability::Unsupported
                                   ? tr("Unsupported: %1").arg(property.reason)
                                   : tr("Unavailable: %1").arg(property.reason);
            auto* item = items[columnFor.value(property.name)];
            item->setText(value);
            item->setToolTip(value);
        }
        for (int column = 0; column < items.size(); ++column)
            model_->setItem(modelRow, column, items[column]);
        ++modelRow;
    }
    // perf-ok: object metadata; precision limits the sample to 50 rows.
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
void ObjectExplorer::renderGraph(const ObjectGraph& graph) {
    if (graph.availability == MetadataAvailability::Unsupported) {
        erd_->clearGraph();
        graphState_ = "unsupported";
        graphMessage_ = tr("Unsupported: %1").arg(graph.reason);
        setStatus(graphState_, graphMessage_);
        return;
    }
    if (std::none_of(graph.tables.begin(), graph.tables.end(),
                     [this](const ObjectGraphTable& table) { return table.id == object_; })) {
        erd_->clearGraph();
        graphState_ = "incomplete";
        graphMessage_ =
            tr("%1 · ERD incomplete: selected table metadata is unavailable").arg(label_);
        setStatus(graphState_, graphMessage_);
        return;
    }
    erd_->setGraph(graph, object_);
    if (graph.availability == MetadataAvailability::Unavailable || !graph.warnings.isEmpty()) {
        graphState_ = "incomplete";
        QStringList details;
        if (!graph.reason.isEmpty())
            details.append(graph.reason);
        details.append(graph.warnings);
        graphMessage_ = tr("%1 · ERD incomplete: %2").arg(label_, details.join("; "));
    } else if (graph.edges.isEmpty()) {
        graphState_ = "empty";
        graphMessage_ = tr("%1 · No foreign-key relationships").arg(label_);
    } else {
        graphState_ = "loaded";
        graphMessage_ = tr("%1 · %2 related tables, %3 foreign keys")
                            .arg(label_)
                            .arg(qMax(0, graph.tables.size() - 1))
                            .arg(graph.edges.size());
    }
    setStatus(graphState_, graphMessage_);
}
void ObjectExplorer::installDataWidget(QWidget* widget) {
    auto* previous = pages_->widget(3);
    for (auto& action : dataHeaderActions_)
        if (action)
            action->deleteLater();
    dataHeaderActions_.clear();
    if (dataFooter_) {
        layout()->removeWidget(dataFooter_);
        dataFooter_->setParent(previous);
        dataFooter_ = nullptr;
    }
    pages_->removeWidget(previous);
    pages_->insertWidget(3, widget);
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
            dataFooter_->setParent(this);
            static_cast<QVBoxLayout*>(layout())->addWidget(dataFooter_);
        }
    }
    updateFooter();
    previous->deleteLater();
}
void ObjectExplorer::updateFooter() {
    const bool dataVisible = dataFooter_ && tabs_->currentIndex() == 5;
    for (const auto& action : dataHeaderActions_)
        if (action)
            action->setEnabled(dataVisible && connection_.has_value());
    if (dataFooter_)
        dataFooter_->setVisible(dataVisible);
    footer_->setVisible(!dataVisible);
    refresh_->setEnabled(!operationBusy_ && connection_.has_value() && !requestToken_);
}
void ObjectExplorer::setOperationBusy(bool busy) {
    operationBusy_ = busy;
    auto* line = footer_;
    line->setBusy(busy || status_->property("state") == "loading");
    if (!busy)
        line->setMessage({});
    updateActions();
    refresh_->setEnabled(!busy && connection_.has_value() && !requestToken_);
    if (busy)
        activePane_ = tabs_->currentIndex();
    else if (status_->property("state") == "busy")
        setStatus("ready", tr("%1 · %2").arg(label_, tabs_->tabText(tabs_->currentIndex())));
}
void ObjectExplorer::updateActions() {
    const bool ready = connection_.has_value() && !operationBusy_ && !basicObject(kind_);
    const auto unavailable =
        !connection_     ? tr("Reconnect this object's connection to use SQL actions.")
        : operationBusy_ ? tr("Finish or cancel the active Data operation.")
                         : tr("SQL generation is unavailable for this object type.");
    open_->setEnabled(ready);
    open_->setToolTip(ready ? tr("Open a new query draft for this object.") : unavailable);
    generate_->setEnabled(ready);
    generate_->setToolTip(ready ? tr("Generate SQL without running it.") : unavailable);
    for (auto it = generationActions_.begin(); it != generationActions_.end(); ++it) {
        const auto reason =
            SqlTemplateService::unavailableReason(it.key(), columnsLoaded_, !columns_.isEmpty());
        it.value()->setEnabled(ready && reason.isEmpty());
        it.value()->setToolTip(ready ? reason : unavailable);
    }
}
void ObjectExplorer::generateSql(const QString& kind) {
    if (!connection_ || operationBusy_ || basicObject(kind_))
        return;
    if (const auto reason =
            SqlTemplateService::unavailableReason(kind, columnsLoaded_, !columns_.isEmpty());
        !reason.isEmpty()) {
        setStatus("unavailable", reason);
        return;
    }
    const auto result = SqlTemplateService::generate(kind, label_, columns_, columnsLoaded_);
    if (!result.valid) {
        setStatus("failed", result.error);
        return;
    }
    emit sqlGenerated(*connection_, result.sql);
}
} // namespace choscordb
