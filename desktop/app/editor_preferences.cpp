#include "editor_preferences.h"
#include "widgets/preferences_dialog.h"
#include "widgets/sql_editor.h"
#include <QAction>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMainWindow>
#include <QStatusBar>
#include <Qsci/qscicommand.h>
#include <Qsci/qscicommandset.h>
#include <utility>
namespace choscordb {
namespace {
constexpr quint64 loadToken = quint64(1) << 56;
}
EditorPreferencesController::EditorPreferencesController(QMainWindow* window)
    : QObject(window), window_(window) {
    const auto limits = EngineAdapter::editorPreferenceLimits();
    preferences_.version = limits.version;
    preferences_.fontSize = limits.defaultFontSize;
}
void EditorPreferencesController::addAction(const QString& id, QAction* action, bool configurable) {
    actions_.insert(id, action);
    if (configurable && action->objectName().isEmpty())
        action->setObjectName("command_" + id);
    catalog_.append({id, action->text(), action->shortcut().toString(QKeySequence::PortableText),
                     configurable});
}
void EditorPreferencesController::addEditor(SqlEditor* editor) {
    // Application actions own these shortcuts so changing or disabling them also
    // removes Scintilla's built-in primary and alternate bindings.
    for (auto id : {QsciCommand::Undo, QsciCommand::Redo, QsciCommand::SelectionCut,
                    QsciCommand::SelectionCopy, QsciCommand::Paste}) {
        if (auto* command = editor->standardCommands()->find(id)) {
            command->setKey(0);
            command->setAlternateKey(0);
        }
    }
    editors_.removeIf([](const auto& item) { return item.isNull(); });
    editors_.append(editor);
    editor->installEventFilter(this);
    applyFont(editor);
}
bool EditorPreferencesController::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride) {
        const auto* key = static_cast<QKeyEvent*>(event);
        for (auto* action : actions_) {
            const auto sequence = action->shortcut();
            if (action->isEnabled() && !sequence.isEmpty() &&
                sequence[0] == key->keyCombination()) {
                // Let Qt route configured application shortcuts before Scintilla
                // claims the same key for one of its built-in editing commands.
                event->ignore();
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}
void EditorPreferencesController::applyFont(SqlEditor* editor) {
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!preferences_.fontFamily.isEmpty())
        font.setFamily(preferences_.fontFamily);
    font.setPointSize(preferences_.fontSize);
    editor->setEditorFont(font);
}
void EditorPreferencesController::initialize(EngineAdapter* adapter) {
    adapter_ = adapter;
    connect(adapter, &EngineAdapter::editorPreferencesReady, this,
            [this](quint64 token, const EditorPreferences& value) {
                if ((token == loadToken && std::exchange(loading_, false)) ||
                    pendingSaves_.remove(token))
                    apply(value);
            });
    connect(adapter, &EngineAdapter::recoveryFailed, this,
            [this](quint64 token, const QString& error) {
                pendingSaves_.remove(token);
                if (token == loadToken) {
                    loading_ = false;
                    window_->statusBar()->showMessage(
                        tr("Preferences could not be loaded: %1. Open Preferences to retry.")
                            .arg(error));
                }
            });
    adapter_->getEditorPreferences(loadToken);
}
void EditorPreferencesController::apply(const EditorPreferences& value) {
    const auto error = shortcutValidationError(value, catalog_);
    if (!error.isEmpty()) {
        window_->statusBar()->showMessage(error);
        return;
    }
    preferences_ = value;
    for (const auto& descriptor : catalog_) {
        QString sequence = descriptor.defaultSequence;
        for (const auto& custom : value.shortcuts)
            if (custom.command == descriptor.id)
                sequence = custom.sequence;
        actions_.value(descriptor.id)
            ->setShortcut(QKeySequence::fromString(sequence, QKeySequence::PortableText));
    }
    for (const auto& editor : editors_)
        if (editor)
            applyFont(editor);
}
void EditorPreferencesController::open() {
    if (!adapter_)
        return;
    if (!dialog_) {
        dialog_ = new PreferencesDialog(adapter_, catalog_, window_);
        dialog_->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog_, &PreferencesDialog::preferencesSaveSubmitted, this,
                [this](quint64 token) { pendingSaves_.insert(token); });
        connect(dialog_, &PreferencesDialog::preferencesConfirmed, this,
                &EditorPreferencesController::apply);
    }
    dialog_->show();
    dialog_->raise();
    dialog_->activateWindow();
}
} // namespace choscordb
