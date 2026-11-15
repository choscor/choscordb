#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QComboBox>

namespace choscordb {
namespace {
quint64 position(SqlEditor* editor, int message) {
    return static_cast<quint64>(editor->SendScintilla(message));
}
}

void QueryWorkspace::applySqlEdit(SqlEditor* editor, const SqlTextEdit& edit) {
    if (!edit.valid) {
        message(edit.error);
        return;
    }
    const auto bytes = edit.text.toUtf8();
    const auto end = edit.start + static_cast<quint64>(bytes.size());
    editor->SendScintilla(QsciScintilla::SCI_BEGINUNDOACTION);
    editor->SendScintilla(QsciScintilla::SCI_SETTARGETRANGE, edit.start, edit.end);
    editor->SendScintilla(QsciScintilla::SCI_REPLACETARGET, static_cast<long>(bytes.size()),
                          bytes.constData());
    editor->SendScintilla(QsciScintilla::SCI_ENDUNDOACTION);
    editor->SendScintilla(QsciScintilla::SCI_SETSEL, edit.start, end);
}

void QueryWorkspace::formatSql(bool minify, quint8 indent, bool uppercaseKeywords) {
    auto* editor = widgets_.currentEditor();
    if (!editor || editor->isReadOnly())
        return;
    const auto connection = selectedConnection().value_or(0);
    applySqlEdit(editor, EngineAdapter::formatSql(
                             editor->text(), position(editor, QsciScintilla::SCI_GETCURRENTPOS),
                             position(editor, QsciScintilla::SCI_GETSELECTIONSTART),
                             position(editor, QsciScintilla::SCI_GETSELECTIONEND),
                             driverForConnection(connection), connectionSqlModes_.value(connection),
                             indent, uppercaseKeywords, minify));
}

void QueryWorkspace::setupRowLimit() {
    if (!widgets_.rowLimit)
        return;
    for (const auto choice : EngineAdapter::rowLimitChoices())
        widgets_.rowLimit->addItem(
            choice == 0 ? tr("No row limit") : tr("Limit %L1 rows").arg(choice), choice);
    connect(widgets_.rowLimit, &QComboBox::activated, this, [this](int index) {
        if (const auto connection = selectedConnection())
            connectionRowLimits_.insert(*connection, widgets_.rowLimit->itemData(index).toUInt());
    });
    syncRowLimit();
}

void QueryWorkspace::syncRowLimit() {
    if (!widgets_.rowLimit)
        return;
    const auto connection = selectedConnection();
    widgets_.rowLimit->setEnabled(connection.has_value());
    const auto index = widgets_.rowLimit->findData(
        connection ? connectionRowLimits_.value(*connection) : quint32(0));
    if (index >= 0 && index != widgets_.rowLimit->currentIndex())
        widgets_.rowLimit->setCurrentIndex(index);
}

void QueryWorkspace::toggleSqlComment(bool block) {
    auto* editor = widgets_.currentEditor();
    if (!editor || editor->isReadOnly())
        return;
    applySqlEdit(editor, EngineAdapter::toggleSqlComment(
                             editor->text(), position(editor, QsciScintilla::SCI_GETSELECTIONSTART),
                             position(editor, QsciScintilla::SCI_GETSELECTIONEND), block));
}
}
