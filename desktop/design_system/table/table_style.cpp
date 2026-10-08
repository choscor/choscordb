#include "design_system/table/table_style.h"

#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"
#include "design_system/style/style_resource.h"
#include "design_system/theme.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QHeaderView>
#include <QHelpEvent>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QPainter>
#include <QTableView>
#include <QTimer>
#include <QToolTip>

namespace choscordb::design {
namespace {
QRect linkRect(const QRect& cell) {
    const int width = dimension(Dimension::IconSmall) + 2 * spacing(Spacing::Two);
    return QRect(cell.right() - width + 1, cell.top(), width, cell.height());
}

bool hasLink(const QModelIndex& index) {
    if (!index.isValid())
        return false;
    // Producers with a cheap boolean role avoid formatting the label for every paint.
    if (const auto link = index.data(ForeignKeyLinkRole); link.isValid())
        return link.toBool();
    return !index.data(ForeignKeyLinkLabelRole).toString().isEmpty();
}

QStringList choices(const QModelIndex& index) {
    if (!(index.flags() & Qt::ItemIsEditable))
        return {};
    return index.data(ChoiceLabelsRole).toStringList();
}
} // namespace

ResultTableDelegate::ResultTableDelegate(QTableView& table)
    : QStyledItemDelegate(&table), table_(table) {
    linkAction_ = new QAction(tr("Open referenced row"), &table);
    linkAction_->setObjectName(QStringLiteral("resultCellOpenReference"));
    linkAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Return));
    linkAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    table.addAction(linkAction_);
    connect(linkAction_, &QAction::triggered, this, [this] {
        const auto index = table_.currentIndex();
        if (hasLink(index))
            emit linkActivated(index);
    });
    table.installEventFilter(this);
    table.viewport()->installEventFilter(this);
    bindSelectionModel();
}

void ResultTableDelegate::bindSelectionModel() {
    auto* current = table_.selectionModel();
    if (current == selectionModel_ && table_.model() == model_)
        return;
    if (selectionModel_)
        disconnect(selectionModel_, nullptr, this, nullptr);
    selectionModel_ = current;
    if (current)
        connect(current, &QItemSelectionModel::currentChanged, this,
                [this] { updateLinkAction(); });
    if (model_)
        disconnect(model_, nullptr, this, nullptr);
    model_ = table_.model();
    if (model_) {
        connect(model_, &QAbstractItemModel::dataChanged, this, [this] { updateLinkAction(); });
        connect(model_, &QAbstractItemModel::modelReset, this, [this] { updateLinkAction(); });
    }
    updateLinkAction();
}

void ResultTableDelegate::updateLinkAction() {
    const auto label = table_.currentIndex().data(ForeignKeyLinkLabelRole).toString();
    linkAction_->setEnabled(!label.isEmpty());
    linkAction_->setText(label.isEmpty() ? tr("Open referenced row") : label);
    linkAction_->setToolTip(label);
}

const ResolvedTheme& ResultTableDelegate::theme(const QWidget& widget) const {
    const auto palette = widget.palette().cacheKey();
    if (!theme_ || themePalette_ != palette) {
        theme_ = resolvedThemeForWidget(widget);
        themePalette_ = palette;
        linkPixmaps_.clear();
    }
    return *theme_;
}

QPixmap ResultTableDelegate::linkPixmap(const QColor& color, int size, qreal ratio) const {
    for (const auto& cached : linkPixmaps_)
        if (cached.color == color.rgba() && cached.size == size &&
            qFuzzyCompare(cached.ratio, ratio))
            return cached.pixmap;
    // Normal and selected link glyphs, at most a few device pixel ratios.
    if (linkPixmaps_.size() >= 4)
        linkPixmaps_.clear();
    auto pixmap = themedIcon(Icon::Link, color, size).pixmap(QSize(size, size), ratio);
    linkPixmaps_.push_back({color.rgba(), size, ratio, pixmap});
    return pixmap;
}

bool ResultTableDelegate::eventFilter(QObject* watched, QEvent* event) {
    if (watched == &table_ &&
        (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)) {
        theme_.reset();
        linkPixmaps_.clear();
    }
    if (watched == &table_ && event->type() == QEvent::ChildAdded)
        QTimer::singleShot(0, this, [this] { bindSelectionModel(); });
    if (watched == table_.viewport() &&
        (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::KeyRelease))
        QTimer::singleShot(0, this, [this] { updateLinkAction(); });
    return QStyledItemDelegate::eventFilter(watched, event);
}

void ResultTableDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    QStyleOptionViewItem rowOption(option);
    initStyleOption(&rowOption, index);
    // Qt 6.8 switches a hovered stylesheet item to a Windows-style painter,
    // which changes the native macOS text geometry. Preserve row presentation.
    rowOption.state &= ~QStyle::State_MouseOver;
    auto* style = rowOption.widget ? rowOption.widget->style() : QApplication::style();
    if (const auto change = index.data(CellChangeRole).toInt();
        change != int(CellChange::None) && option.widget) {
        const auto& colors = theme(*option.widget).colors;
        rowOption.backgroundBrush = change == int(CellChange::Deleted)    ? colors.dangerSurface
                                    : change == int(CellChange::Inserted) ? colors.successSurface
                                                                          : colors.warningSurface;
    }
    const bool link = hasLink(index);
    if (link) {
        auto fullBackground = rowOption;
        fullBackground.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &fullBackground, painter,
                           fullBackground.widget);
        rowOption.rect.setRight(linkRect(option.rect).left() - 1);
    }
    style->drawControl(QStyle::CE_ItemViewItem, &rowOption, painter, rowOption.widget);
    if (!link || !option.widget)
        return;
    const QRect hit = linkRect(option.rect);
    const int iconSize = dimension(Dimension::IconSmall);
    const QRect iconRect(hit.center().x() - iconSize / 2, hit.center().y() - iconSize / 2, iconSize,
                         iconSize);
    const auto& colors = theme(*option.widget).colors;
    painter->drawPixmap(
        iconRect,
        linkPixmap(option.state & QStyle::State_Selected ? colors.selectionText : colors.action,
                   iconSize, painter->device() ? painter->device()->devicePixelRatioF() : 1.0));
}

QWidget* ResultTableDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                                           const QModelIndex& index) const {
    const auto labels = choices(index);
    if (labels.isEmpty())
        return QStyledItemDelegate::createEditor(parent, option, index);
    auto* combo = new QComboBox(parent);
    combo->setEditable(false);
    combo->setObjectName(QStringLiteral("resultCellChoiceEditor"));
    combo->setAccessibleName(tr("Cell choices"));
    combo->addItems(labels);
    if (index.data(ChoiceNullableRole).toBool())
        combo->addItem(tr("NULL"), true);
    connect(combo, &QComboBox::activated, combo,
            [delegate = const_cast<ResultTableDelegate*>(this), combo](int) {
                emit delegate->commitData(combo);
                emit delegate->closeEditor(combo);
            });
    return combo;
}

void ResultTableDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    auto* combo = qobject_cast<QComboBox*>(editor);
    if (!combo) {
        QStyledItemDelegate::setEditorData(editor, index);
        return;
    }
    const QString value = index.data(Qt::EditRole).toString();
    int selected = combo->findText(value);
    if (index.data(CellNullRole).toBool() && index.data(ChoiceNullableRole).toBool())
        selected = combo->count() - 1;
    combo->setCurrentIndex(selected);
}

void ResultTableDelegate::setModelData(QWidget* editor, QAbstractItemModel* model,
                                       const QModelIndex& index) const {
    auto* combo = qobject_cast<QComboBox*>(editor);
    if (!combo) {
        QStyledItemDelegate::setModelData(editor, model, index);
        return;
    }
    if (combo->currentIndex() < 0)
        return;
    if (combo->currentData().toBool())
        model->setData(index, {}, TypedNullEditRole);
    else
        model->setData(index, combo->currentText(), Qt::EditRole);
}

bool ResultTableDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                      const QStyleOptionViewItem& option,
                                      const QModelIndex& index) {
    if (hasLink(index) &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease ||
         event->type() == QEvent::MouseButtonDblClick)) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && linkRect(option.rect).contains(mouse->pos())) {
            if (event->type() == QEvent::MouseButtonRelease)
                emit linkActivated(index);
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

bool ResultTableDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view,
                                    const QStyleOptionViewItem& option, const QModelIndex& index) {
    if (hasLink(index) && linkRect(option.rect).contains(event->pos())) {
        QToolTip::showText(event->globalPos(), index.data(ForeignKeyLinkLabelRole).toString(),
                           view);
        return true;
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

QString tableStyleSheet() {
    return loadStyleSheet(QStringLiteral("table/table_style_sheet.qss"));
}

QString tableItemStyleSheet() {
    return loadStyleSheet(QStringLiteral("table/table_item_style_sheet.qss"));
}

ResultTableDelegate* configureResultTable(QTableView& table, bool showGrid) {
    table.setShowGrid(showGrid);
    table.setGridStyle(Qt::SolidLine);
    table.horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    // QTableView::sizeHintForColumn samples rows by the vertical header's precision.
    table.verticalHeader()->setResizeContentsPrecision(50);
    auto* delegate = new ResultTableDelegate(table);
    table.setItemDelegate(delegate);
    return delegate;
}
} // namespace choscordb::design
