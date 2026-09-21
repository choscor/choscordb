#include "design_system/select/select_popup.h"
#include "design_system/menu/embedded_popup.h"
#include "design_system/style/style_resource.h"
#include "design_system/theme.h"
#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QComboBox>
#include <QFontComboBox>
#include <QHelpEvent>
#include <QPointer>
#include <QStyledItemDelegate>

namespace choscordb::design::detail {
namespace {
// Qt's private combo list view copies the combo font into every item option,
// overriding the view font. Forward to the installed delegate so custom item
// painting and separator behavior remain intact.
class NormalFontPopupDelegate final : public QAbstractItemDelegate {
  public:
    NormalFontPopupDelegate(QAbstractItemDelegate* delegate, QObject* parent)
        : QAbstractItemDelegate(parent), delegate_(delegate), fallback_(this) {
        connect(delegate, &QAbstractItemDelegate::commitData, this,
                &QAbstractItemDelegate::commitData);
        connect(delegate, &QAbstractItemDelegate::closeEditor, this,
                &QAbstractItemDelegate::closeEditor);
        connect(delegate, &QAbstractItemDelegate::sizeHintChanged, this,
                &QAbstractItemDelegate::sizeHintChanged);
        connect(&fallback_, &QAbstractItemDelegate::commitData, this,
                &QAbstractItemDelegate::commitData);
        connect(&fallback_, &QAbstractItemDelegate::closeEditor, this,
                &QAbstractItemDelegate::closeEditor);
        connect(&fallback_, &QAbstractItemDelegate::sizeHintChanged, this,
                &QAbstractItemDelegate::sizeHintChanged);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        auto normal = option;
        normal.font.setWeight(QFont::Normal);
        activeDelegate()->paint(painter, normal, index);
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto normal = option;
        normal.font.setWeight(QFont::Normal);
        return activeDelegate()->sizeHint(normal, index);
    }
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override {
        return activeDelegate()->createEditor(parent, option, index);
    }
    void destroyEditor(QWidget* editor, const QModelIndex& index) const override {
        activeDelegate()->destroyEditor(editor, index);
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        activeDelegate()->setEditorData(editor, index);
    }
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override {
        activeDelegate()->setModelData(editor, model, index);
    }
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const override {
        activeDelegate()->updateEditorGeometry(editor, option, index);
    }
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override {
        return activeDelegate()->editorEvent(event, model, option, index);
    }
    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option,
                   const QModelIndex& index) override {
        return activeDelegate()->helpEvent(event, view, option, index);
    }
    QList<int> paintingRoles() const override { return activeDelegate()->paintingRoles(); }

  private:
    QAbstractItemDelegate* activeDelegate() const {
        return delegate_ ? delegate_.data() : &fallback_;
    }
    QPointer<QAbstractItemDelegate> delegate_;
    mutable QStyledItemDelegate fallback_;
};
} // namespace

bool handleFontComboResize(QComboBox& combo, QEvent* event) {
    if (event->type() != QEvent::Resize || !qobject_cast<QFontComboBox*>(&combo) ||
        combo.property("designForwardingFontResize").toBool())
        return false;
    QPointer<QWidget> popup = combo.view()->parentWidget();
    if (!popup->property("embeddedPopupOwner").isValid())
        return false;
    // QFontComboBox::event resizes view()->window(), assuming the view still
    // belongs to a native popup. Give that one Qt event its original hierarchy,
    // then restore the child surface. Forwarding preserves QComboBox's normal
    // resize handling, including its editable line field geometry.
    QPointer<QComboBox> origin = &combo;
    combo.setProperty("designForwardingFontResize", true);
    combo.hidePopup();
    popup->setParent(&combo, (popup->windowFlags() & ~Qt::WindowType_Mask) | Qt::Popup);
    popup->hide();
    QApplication::sendEvent(&combo, event);
    if (origin) {
        origin->setProperty("designForwardingFontResize", false);
        if (popup)
            embedPopup(popup, origin);
    }
    return true;
}

void prepareComboPopup(QComboBox& combo) {
    auto* view = combo.view();
    if (!dynamic_cast<NormalFontPopupDelegate*>(view->itemDelegate()))
        view->setItemDelegate(new NormalFontPopupDelegate(view->itemDelegate(), view));
    auto* popup = view->parentWidget();
    const auto theme = resolvedThemeForWidget(combo);
    const auto& colors = theme.colors;
    popup->setObjectName("designComboPopup");
    popup->setWindowFlag(Qt::NoDropShadowWindowHint);
    popup->setAttribute(Qt::WA_TranslucentBackground);
    popup->setAttribute(Qt::WA_MacShowFocusRect, false);
    popup->setPalette(applicationPalette(theme));
    // Qt deliberately excludes its private combo container from inherited QSS.
    // Keep the container borderless so its frame does not double the view's
    // visible border. Retain the native list/delegate for custom models.
    popup->setStyleSheet(
        loadStyleSheet(QStringLiteral("select/popup.qss")).arg(colors.popover.name()));
    embedPopup(popup, &combo);
    view->setAttribute(Qt::WA_MacShowFocusRect, false);
    view->setPalette(applicationPalette(theme));
    view->setStyleSheet(loadStyleSheet(QStringLiteral("select/popup_view.qss"))
                            .arg(colors.popover.name(), colors.foreground.name(),
                                 colors.accent.name(), colors.muted.name(), colors.disabled.name(),
                                 colors.border.name()));
}

} // namespace choscordb::design::detail
