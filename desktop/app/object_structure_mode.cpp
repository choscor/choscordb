#include "app/object_structure_mode.h"
#include "bridge/engine_adapter.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/metrics/metrics.h"
#include "design_system/toast_region/toast_region.h"
#include "models/navigator_model.h"
#include "widgets/sql_review_dialog/sql_review_dialog.h"
#include "widgets/structure_editor/structure_editor.h"
#include <QAction>
#include <QHBoxLayout>
#include <QTabBar>
#include <QVBoxLayout>

namespace choscordb {
namespace {
std::optional<ObjectInspectionPane> structurePane(int index) {
    for (const auto pane :
         {ObjectInspectionPane::Columns, ObjectInspectionPane::Indexes, ObjectInspectionPane::Keys})
        if (index == int(pane))
            return pane;
    return std::nullopt;
}
}
ObjectStructureMode::ObjectStructureMode(EngineAdapter* adapter, QTabBar* panes, QWidget* parent)
    : QWidget(parent), adapter_(adapter), panes_(panes) {
    setObjectName("objectStructureMode");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        design::spacing(design::Spacing::Two), design::spacing(design::Spacing::Two),
        design::spacing(design::Spacing::Two), design::spacing(design::Spacing::Two));
    auto* actions = new QWidget(parent);
    actions->setObjectName("objectStructureActions");
    auto* actionLayout = new QHBoxLayout(actions);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(design::spacing(design::Spacing::Two));
    const auto button = [actions, actionLayout](const QString& text, const char* name,
                                                design::ButtonVariant variant) {
        auto* created = new design::Button(text, actions);
        created->setObjectName(name);
        created->setVariant(variant);
        created->setButtonSize(design::ButtonSize::Small);
        actionLayout->addWidget(created);
        return created;
    };
    edit_ = button(tr("Edit structure"), "objectStructureEdit", design::ButtonVariant::Outline);
    discard_ = button(tr("Discard"), "objectStructureDiscard", design::ButtonVariant::Outline);
    discard_->setToolTip(tr("Leave structure editing and discard the staged changes."));
    review_ = button(tr("Apply…"), "objectStructureReview", design::ButtonVariant::Default);
    review_->setToolTip(tr("Review the SQL of the staged structure changes before you apply it."));
    connect(edit_, &QPushButton::clicked, this, [this] {
        if (const auto reason = begin(); !reason.isEmpty())
            emit statusChanged(reason, true, false);
    });
    connect(discard_, &QPushButton::clicked, this, [this] { resolvePendingChanges(); });
    connect(review_, &QPushButton::clicked, this, &ObjectStructureMode::review);
    reviewAction_ = new QAction(review_->text(), this);
    reviewAction_->setObjectName("objectStructureReviewAction");
    discardAction_ = new QAction(discard_->text(), this);
    discardAction_->setObjectName("objectStructureDiscardAction");
    for (auto* action : {reviewAction_, discardAction_}) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        addAction(action);
    }
    connect(reviewAction_, &QAction::triggered, this, &ObjectStructureMode::review);
    connect(discardAction_, &QAction::triggered, this, [this] { resolvePendingChanges(); });
    connect(panes_, &QTabBar::currentChanged, this, &ObjectStructureMode::refresh);
    refresh();
}
QWidget* ObjectStructureMode::headerActions() const {
    return edit_->parentWidget();
}
void ObjectStructureMode::setDriverResolver(std::function<QString(quint64)> resolver) {
    driverResolver_ = std::move(resolver);
}
void ObjectStructureMode::setShortcuts(const QList<QKeySequence>& review,
                                       const QList<QKeySequence>& discard) {
    reviewAction_->setShortcuts(review);
    discardAction_->setShortcuts(discard);
}
void ObjectStructureMode::setObject(std::optional<quint64> connection, const QString& objectId,
                                    const QString& kind, const QVariantList& properties,
                                    bool blocked) {
    if (editor_ && objectId != objectId_)
        end();
    connection_ = connection;
    objectId_ = objectId;
    blocked_ = blocked;
    relation_ = EngineAdapter::objectKindTraits(kind).relation;
    unavailable_ = EngineAdapter::structureUnavailableReason(
        kind, NavigatorModel::relationSubtype(properties));
    refresh();
}
QString ObjectStructureMode::unavailableReason() const {
    if (!connection_)
        return tr("Reconnect this object's connection to edit its structure.");
    if (blocked_)
        return tr("Finish or cancel the active Data operation.");
    if (!driverResolver_)
        return tr("The driver of this connection is not known.");
    return unavailable_;
}
QString ObjectStructureMode::begin() {
    if (!editor_) {
        if (const auto reason = unavailableReason(); !reason.isEmpty())
            return reason;
        StructureTable table;
        table.driver = driverResolver_(*connection_);
        if (table.driver.isEmpty())
            return tr("The driver of this connection is not known.");
        editor_ = new StructureEditor(adapter_, *connection_, std::move(table), objectId_, this);
        editor_->setObjectName("objectStructureEditor");
        layout()->addWidget(editor_);
        connect(editor_, &StructureEditor::stateChanged, this, &ObjectStructureMode::refresh);
        connect(editor_, &StructureEditor::statusChanged, this,
                &ObjectStructureMode::statusChanged);
        connect(editor_, &StructureEditor::applyFailed, this,
                [this](quint64 connection) { emit structureChanged(connection); });
        connect(editor_, &StructureEditor::applied, this, [this](quint64 connection) {
            const auto count = int(editor_->changeCount());
            end();
            emit structureChanged(connection);
            if (auto* toast = windowToast(this))
                toast->showToast(tr("Structure changed"),
                                 tr("Applied %n change(s).", nullptr, count),
                                 ToastVariant::Success);
        });
        editor_->load();
    }
    if (!structurePane(panes_->currentIndex()))
        panes_->setCurrentIndex(int(ObjectInspectionPane::Columns));
    refresh();
    return {};
}
bool ObjectStructureMode::resolvePendingChanges() {
    if (!editor_)
        return true;
    if (editor_->isApplying()) {
        emit statusChanged(tr("Wait for the structure changes to finish."), true, false);
        return false;
    }
    if (editor_->hasChanges() &&
        !ConfirmationDialog::confirm(window(), tr("Discard structure changes?"),
                                     tr("The staged structure changes are not applied."),
                                     tr("Discard changes"),
                                     ConfirmationDialog::Consequence::Destructive))
        return false;
    end();
    return true;
}
void ObjectStructureMode::end() {
    if (!editor_)
        return;
    editor_->disconnect(this);
    editor_->hide();
    editor_->deleteLater();
    editor_ = nullptr;
    refresh();
}
void ObjectStructureMode::review() {
    if (!editor_ || blocked_)
        return;
    const QPointer<StructureEditor> editor(editor_);
    const auto sql = editor->plannedSql();
    if (!editor->canApply())
        return;
    const bool accepted = confirmSqlReview(
        window(), {QStringLiteral("structureChange"), tr("Review structure changes"),
                   tr("Close structure change review"),
                   tr("The statements run in this order. Apply changes runs them now."), sql,
                   tr("Apply changes")});
    if (accepted && editor && editor == editor_ && !blocked_)
        editor->apply(sql);
}
void ObjectStructureMode::refresh() {
    const auto pane = structurePane(panes_->currentIndex());
    const auto reason = unavailableReason();
    const bool editing = editor_ != nullptr;
    edit_->setVisible(!editing);
    edit_->setEnabled(reason.isEmpty());
    edit_->setToolTip(reason.isEmpty() ? tr("Edit the columns, indexes and keys of this table.")
                                       : reason);
    edit_->parentWidget()->setVisible(relation_ && pane.has_value());
    discard_->setVisible(editing);
    discard_->setEnabled(editing && !editor_->isApplying());
    review_->setVisible(editing);
    review_->setEnabled(editing && !blocked_ && editor_->canApply());
    reviewAction_->setEnabled(review_->isEnabled() && pane.has_value());
    discardAction_->setEnabled(discard_->isEnabled() && pane.has_value());
    if (editing)
        editor_->setPane(pane);
    setVisible(editing && pane.has_value());
}
}
