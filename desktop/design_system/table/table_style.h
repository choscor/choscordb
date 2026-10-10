#pragma once

#include "design_system/theme.h"

#include <QModelIndex>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QStyledItemDelegate>
#include <optional>
#include <vector>
class QTableView;
class QAction;
class QItemSelectionModel;
class QAbstractItemModel;

namespace choscordb::design {
// Neutral cell presentation roles. Producers retain all eligibility decisions.
inline constexpr int ChoiceLabelsRole = Qt::UserRole + 20;
inline constexpr int ChoiceNullableRole = Qt::UserRole + 21;
inline constexpr int ForeignKeyLinkLabelRole = Qt::UserRole + 22;
inline constexpr int TypedNullEditRole = Qt::UserRole + 23;
inline constexpr int CellNullRole = Qt::UserRole + 24;
// Cheap boolean form of ForeignKeyLinkLabelRole for painting and hit testing. Models that
// do not provide it fall back to a non-empty label.
inline constexpr int ForeignKeyLinkRole = Qt::UserRole + 25;
// Staged-change state painted with theme surfaces by ResultTableDelegate.
inline constexpr int CellChangeRole = Qt::UserRole + 26;
enum class CellChange { None, Changed, Inserted, Deleted };

class ResultTableDelegate final : public QStyledItemDelegate {
    Q_OBJECT
  public:
    explicit ResultTableDelegate(QTableView& table);
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;
    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option,
                   const QModelIndex& index) override;

  signals:
    void linkActivated(const QModelIndex& index);

  private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void bindSelectionModel();
    void updateLinkAction();
    // Painting reuses the resolved theme and rendered link glyphs until the table's
    // palette, style, or device pixel ratio changes.
    const ResolvedTheme& theme(const QWidget& widget) const;
    QPixmap linkPixmap(const QColor& color, int size, qreal ratio) const;
    struct LinkPixmap {
        QRgb color;
        int size;
        qreal ratio;
        QPixmap pixmap;
    };
    mutable std::optional<ResolvedTheme> theme_;
    mutable qint64 themePalette_ = 0;
    mutable std::vector<LinkPixmap> linkPixmaps_;
    QTableView& table_;
    QAction* linkAction_ = nullptr;
    QPointer<QItemSelectionModel> selectionModel_;
    QPointer<QAbstractItemModel> model_;
};

ResultTableDelegate* configureResultTable(QTableView& table, bool showGrid = true);
} // namespace choscordb::design
