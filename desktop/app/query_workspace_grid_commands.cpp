#include "app/query_workspace.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/table/table_style.h"

#include <QAction>
#include <QApplication>
#include <QItemSelectionRange>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMetaProperty>
#include <QPointer>
#include <QPushButton>
#include <QTableView>
namespace choscordb {
namespace {
using GridCommand = QueryWorkspace::GridCommand;
// One row for each GridCommand, in enum order.
struct GridCommandSpec {
    const char* id;
    const char* label;
    const char* sequence;
    // The object name of an action that no hidden button names.
    const char* name;
};
constexpr std::array<GridCommandSpec, 8> gridCommands = {{
    {"grid_add_row", QT_TRANSLATE_NOOP("choscordb::QueryWorkspace", "Add row"), "Ctrl+N", nullptr},
    {"grid_duplicate_row", QT_TRANSLATE_NOOP("choscordb::QueryWorkspace", "Duplicate row"),
     "Ctrl+D", "duplicateRow"},
    {"grid_delete_rows", QT_TRANSLATE_NOOP("choscordb::QueryWorkspace", "Delete rows"),
     "Ctrl+Alt+-", nullptr},
    {"grid_restore_rows", QT_TRANSLATE_NOOP("choscordb::QueryWorkspace", "Restore rows"),
     "Ctrl+Alt+=", nullptr},
    {"grid_set_null", QT_TRANSLATE_NOOP("choscordb::QueryWorkspace", "Set NULL"), "Del", nullptr},
    {"grid_edit_cell", QT_TRANSLATE_NOOP("choscordb::QueryWorkspace", "Edit cell…"), "Shift+Return",
     "editCell"},
    {"grid_apply_edits", QT_TRANSLATE_NOOP("choscordb::QueryWorkspace", "Apply…"), "Ctrl+S",
     nullptr},
    {"grid_discard_edits", QT_TRANSLATE_NOOP("choscordb::QueryWorkspace", "Discard"),
     "Ctrl+Shift+Backspace", nullptr},
}};
// Set NULL keeps Backspace as a fixed alternate of its configurable primary key.
constexpr auto setNullAlternate = "Backspace";
QString gridLabel(std::size_t command) {
    return QueryWorkspace::tr(gridCommands[command].label);
}
// Qt's shortcut map also ignores the keypad flag, treats Enter as Return, and
// accepts a symbol typed with Shift when the binding has no Shift.
QList<QKeyCombination> keyCandidates(QKeyCombination key) {
    auto modifiers = key.keyboardModifiers() & ~Qt::KeypadModifier;
    const auto code = key.key() == Qt::Key_Enter ? Qt::Key_Return : key.key();
    QList<QKeyCombination> candidates{QKeyCombination(modifiers, code)};
    const bool symbol = code >= Qt::Key_Exclam && code <= Qt::Key_AsciiTilde &&
                        !(code >= Qt::Key_A && code <= Qt::Key_Z) &&
                        !(code >= Qt::Key_0 && code <= Qt::Key_9);
    if (symbol && modifiers.testFlag(Qt::ShiftModifier))
        candidates.append(QKeyCombination(modifiers & ~Qt::ShiftModifier, code));
    return candidates;
}
} // namespace
QList<ShortcutDescriptor> QueryWorkspace::gridShortcuts() {
    QList<ShortcutDescriptor> rows;
    for (std::size_t i = 0; i < gridCommands.size(); ++i)
        rows.append({gridCommands[i].id, gridLabel(i), gridCommands[i].sequence, true,
                     ShortcutScope::ResultGrid});
    // Fixed grid keys take part in the conflict check so no command can take them.
    rows.append({"grid_set_null_alternate", tr("Set NULL (alternate)"), setNullAlternate, false,
                 ShortcutScope::ResultGrid});
    rows.append({"grid_copy", tr("Copy"),
                 QKeySequence(QKeySequence::Copy).toString(QKeySequence::PortableText), false,
                 ShortcutScope::ResultGrid});
    rows.append({"grid_open_reference", tr("Open referenced row"),
                 QKeySequence(design::OpenReferenceKey).toString(QKeySequence::PortableText), false,
                 ShortcutScope::ResultGrid});
    return rows;
}
void QueryWorkspace::setGridShortcut(const QString& id, const QKeySequence& primary) {
    for (std::size_t i = 0; i < gridCommands.size(); ++i) {
        if (id != QLatin1String(gridCommands[i].id))
            continue;
        QList<QKeySequence> keys;
        if (!primary.isEmpty())
            keys.append(primary);
        if (static_cast<GridCommand>(i) == GridCommand::SetNull)
            keys.append(QKeySequence::fromString(setNullAlternate, QKeySequence::PortableText));
        gridActions_[i]->setShortcuts(keys);
    }
    updateApplyToolTip();
}
void QueryWorkspace::setupGridCommands() {
    const std::array<QPushButton*, 8> buttons = {
        widgets_.addRow,  nullptr, widgets_.deleteRows, widgets_.restoreRows,
        widgets_.setNull, nullptr, widgets_.applyEdits, widgets_.discardEdits};
    for (std::size_t i = 0; i < gridActions_.size(); ++i) {
        const auto command = static_cast<GridCommand>(i);
        auto* action = new QAction(gridLabel(i), widgets_.grid);
        // The menu items keep the object names of the commands they replace.
        action->setObjectName(buttons[i] ? buttons[i]->objectName()
                                         : QString::fromLatin1(gridCommands[i].name));
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        widgets_.grid->addAction(action);
        connect(action, &QAction::triggered, this, [this, command] { runGridCommand(command); });
        if (buttons[i])
            connect(buttons[i], &QPushButton::clicked, this,
                    [this, command] { runGridCommand(command); });
        gridActions_[i] = action;
    }
    for (const auto& command : gridCommands)
        setGridShortcut(command.id,
                        QKeySequence::fromString(command.sequence, QKeySequence::PortableText));
    // The grid takes its keys before window shortcuts with the same keys can match.
    widgets_.grid->installEventFilter(this);
    widgets_.grid->viewport()->installEventFilter(this);
    connect(widgets_.grid->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &QueryWorkspace::updateGridActions);
}
QAction* QueryWorkspace::gridAction(GridCommand command) const {
    return gridActions_[static_cast<std::size_t>(command)];
}
QModelIndex QueryWorkspace::gridTarget() const {
    return menuTarget_ ? QModelIndex(*menuTarget_) : widgets_.grid->currentIndex();
}
void QueryWorkspace::updateGridActions() {
    const auto mirror = [this](GridCommand command, const QPushButton* button) {
        gridAction(command)->setEnabled(button && button->isEnabled());
    };
    mirror(GridCommand::AddRow, widgets_.addRow);
    mirror(GridCommand::DeleteRows, widgets_.deleteRows);
    mirror(GridCommand::RestoreRows, widgets_.restoreRows);
    mirror(GridCommand::SetNull, widgets_.setNull);
    mirror(GridCommand::ApplyEdits, widgets_.applyEdits);
    mirror(GridCommand::DiscardEdits, widgets_.discardEdits);
    const auto target = gridTarget();
    gridAction(GridCommand::DuplicateRow)
        ->setEnabled(gridAction(GridCommand::AddRow)->isEnabled() && target.isValid() &&
                     target.model() == model_);
    gridAction(GridCommand::EditCell)->setEnabled(cellEditEligible(target));
    // The context menu shows each blocked reason as the item's tooltip.
    for (std::size_t i = 0; i < gridActions_.size(); ++i) {
        auto* action = gridActions_[i];
        const auto reason =
            action->isEnabled() ? QString() : gridBlockedReason(static_cast<GridCommand>(i));
        action->setToolTip(reason.isEmpty() ? gridLabel(i) : reason);
    }
}
void QueryWorkspace::updateApplyToolTip() {
    auto* apply = widgets_.applyEdits;
    if (!apply)
        return;
    const auto& blocked = editTransactionGuard().applyEdits;
    const auto key =
        gridAction(GridCommand::ApplyEdits)->shortcut().toString(QKeySequence::NativeText);
    const auto name = apply->accessibleName();
    apply->setToolTip(!blocked.isEmpty() ? blocked : key.isEmpty() ? name : name + " · " + key);
}
QString QueryWorkspace::gridBlockedReason(GridCommand command) const {
    // Busy work and editability planning end by themselves; the key then does nothing.
    if (workInFlight() || editabilityPlanning_)
        return {};
    const auto* selection = widgets_.grid->selectionModel();
    const bool selected = selection && selection->hasSelection();
    const auto target = gridTarget();
    switch (command) {
    case GridCommand::AddRow:
    case GridCommand::DuplicateRow:
        if (model_->columnCount() > 0 && !model_->canInsert())
            return editReason_.isEmpty() ? tr("This result is read only.") : editReason_;
        return {};
    case GridCommand::DeleteRows:
        return selected && !model_->canDelete() && !model_->hasInsertedRows() ? editReason_
                                                                              : QString();
    case GridCommand::SetNull:
        return selected && !model_->hasEditableCell(selection->selection()) ? editReason_
                                                                            : QString();
    case GridCommand::EditCell:
        return target.isValid() && !(model_->flags(target) & Qt::ItemIsEditable) ? editReason_
                                                                                 : QString();
    case GridCommand::ApplyEdits:
        return model_->hasPendingEdits() ? editTransactionGuard().applyEdits : QString();
    case GridCommand::RestoreRows:
    case GridCommand::DiscardEdits:
        return {};
    }
    return {};
}
void QueryWorkspace::runGridCommand(GridCommand command) {
    if (!gridAction(command)->isEnabled()) {
        // A command that a rule blocks shows the rule; one with nothing to act on does nothing.
        if (const auto reason = gridBlockedReason(command); !reason.isEmpty())
            message(reason);
        return;
    }
    const auto target = gridTarget();
    const auto* selection = widgets_.grid->selectionModel();
    switch (command) {
    case GridCommand::AddRow:
        model_->addRow();
        break;
    case GridCommand::DuplicateRow:
        duplicateRow(target);
        break;
    case GridCommand::DeleteRows:
    case GridCommand::RestoreRows:
        // Rust rejects deleting rows it cannot match when the edits are planned.
        if (selection)
            model_->markRowsDeleted(model_->selectedRows(selection->selection()),
                                    command == GridCommand::DeleteRows);
        break;
    case GridCommand::SetNull:
        if (selection)
            model_->setNull(selection->selection());
        break;
    case GridCommand::EditCell:
        openCellEditor(target);
        break;
    case GridCommand::ApplyEdits:
        applyStagedEdits();
        break;
    case GridCommand::DiscardEdits:
        discardStagedEdits();
        break;
    }
}
void QueryWorkspace::duplicateRow(const QModelIndex& index) {
    if (!index.isValid() || index.model() != model_)
        return;
    QString error;
    const bool duplicated = editDuplicable_.size() == static_cast<size_t>(model_->columnCount())
                                ? model_->duplicateRow(index.row(), editDuplicable_, &error)
                                : model_->duplicateRow(index.row(), &error);
    if (!duplicated && !error.isEmpty())
        message(error);
}
void QueryWorkspace::discardStagedEdits() {
    ConfirmationDialog box(QMessageBox::Warning, tr("Discard grid changes"),
                           tr("Discard all staged grid changes?"), QMessageBox::NoButton,
                           widgets_.dialogParent);
    auto* discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    auto* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    const QPointer<QueryWorkspace> self(this);
    box.exec();
    // Work that started while the dialog was open can make the discard stale.
    if (self && box.clickedButton() == discard &&
        gridAction(GridCommand::DiscardEdits)->isEnabled())
        model_->discardEdits();
}
std::optional<QueryWorkspace::GridCommand>
QueryWorkspace::gridCommandForKey(QKeyCombination key) const {
    for (const auto candidate : keyCandidates(key))
        for (std::size_t i = 0; i < gridActions_.size(); ++i)
            for (const auto& sequence : gridActions_[i]->shortcuts())
                if (sequence.count() == 1 && sequence[0] == candidate)
                    return static_cast<GridCommand>(i);
    return std::nullopt;
}
bool QueryWorkspace::commitInlineEditor(QWidget* editor) {
    const QPersistentModelIndex index = widgets_.grid->currentIndex();
    if (!index.isValid() || widgets_.grid->indexWidget(index) != editor)
        return false;
    const auto draft = editor->metaObject()->userProperty().read(editor).toString();
    bool staged = draft == index.data(Qt::EditRole).toString();
    const auto watch = connect(model_, &QAbstractItemModel::dataChanged, this,
                               [&staged, index](const QModelIndex& first, const QModelIndex& last) {
                                   staged =
                                       staged || QItemSelectionRange(first, last).contains(index);
                               });
    auto* delegate = widgets_.grid->itemDelegateForIndex(index);
    emit delegate->commitData(editor);
    disconnect(watch);
    // A value that did not stage keeps its draft open for a fix.
    if (staged)
        emit delegate->closeEditor(editor, QAbstractItemDelegate::NoHint);
    return staged;
}
bool QueryWorkspace::eventFilter(QObject* watched, QEvent* event) {
    auto* viewport = widgets_.grid->viewport();
    // The inline editor and its parts are children of the viewport.
    if (watched == viewport && event->type() == QEvent::ChildPolished)
        if (auto* editor = qobject_cast<QWidget*>(static_cast<QChildEvent*>(event)->child())) {
            editor->installEventFilter(this);
            for (auto* part : editor->findChildren<QWidget*>())
                part->installEventFilter(this);
        }
    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress)
        return QObject::eventFilter(watched, event);
    const auto command = gridCommandForKey(static_cast<QKeyEvent*>(event)->keyCombination());
    if (!command)
        return QObject::eventFilter(watched, event);
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    if (watched != widgets_.grid && watched != viewport) {
        // An open inline editor keeps its keys. Only Apply stages its value first.
        if (*command != GridCommand::ApplyEdits)
            return false;
        auto* editor = static_cast<QWidget*>(watched);
        while (editor->parentWidget() && editor->parentWidget() != viewport)
            editor = editor->parentWidget();
        if (commitInlineEditor(editor))
            runGridCommand(GridCommand::ApplyEdits);
        return true;
    }
    // A key that the inline editor did not use has no grid effect.
    const auto* focus = QApplication::focusWidget();
    if (!focus || focus == viewport || !viewport->isAncestorOf(focus))
        runGridCommand(*command);
    return true;
}
} // namespace choscordb
