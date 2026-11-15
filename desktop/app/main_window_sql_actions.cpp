#include "app/editor_preferences.h"
#include "app/main_window.h"
#include "app/query_workspace.h"
#include <QAction>
#include <QMenu>
#include <QTabWidget>

namespace choscordb {
void MainWindow::addSqlTextActions(QMenu* editMenu) {
    editMenu->addSeparator();
    const auto sqlTextAction = [this, editMenu](const QString& id, const QString& text,
                                                const QKeySequence& shortcut, auto handler) {
        auto* action = editMenu->addAction(text);
        action->setShortcut(shortcut);
        preferences_->addAction(id, action);
        connect(action, &QAction::triggered, this, [this, handler] {
            if (workspace_ && editors_->isEnabled() && editors_->currentWidget() &&
                showScreen(Screen::Sql))
                handler();
        });
        return action;
    };
    const auto format = [this](bool minify) {
        const auto& value = preferences_->preferences();
        workspace_->formatSql(minify, value.formatIndent, value.formatUppercaseKeywords);
    };
    sqlTextAction("format_sql", tr("Format SQL"), QKeySequence(Qt::CTRL | Qt::Key_I),
                  [format] { format(false); });
    sqlTextAction("minify_sql", tr("Minify SQL"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I),
                  [format] { format(true); });
    sqlTextAction("toggle_line_comment", tr("Toggle line comment"),
                  QKeySequence(Qt::CTRL | Qt::Key_Slash),
                  [this] { workspace_->toggleSqlComment(false); });
    sqlTextAction("toggle_block_comment", tr("Toggle block comment"),
                  QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Slash),
                  [this] { workspace_->toggleSqlComment(true); });
    editMenu->addSeparator();
    sqlTextAction("switch_database", tr("Switch database…"), QKeySequence(Qt::CTRL | Qt::Key_K),
                  [this] { switchDatabase(); });
    sqlTextAction("manage_databases", tr("Manage databases…"),
                  QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D), [this] { manageDatabases(); });
}
void MainWindow::addRunSqlFileAction(QMenu* fileMenu) {
    auto* action = fileMenu->addAction(tr("Run SQL file…"));
    action->setObjectName("runSqlFile");
    action->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_O));
    preferences_->addAction("run_sql_file", action);
    connect(action, &QAction::triggered, this, [this] {
        if (workspace_)
            workspace_->runSqlFile();
    });
    for (const bool restore : {false, true}) {
        auto* transfer =
            fileMenu->addAction(restore ? tr("Restore database…") : tr("Back up database…"));
        transfer->setObjectName(restore ? "restoreDatabase" : "backupDatabase");
        preferences_->addAction(restore ? "restore_database" : "backup_database", transfer);
        connect(transfer, &QAction::triggered, this, [this, restore] {
            if (workspace_)
                workspace_->backupDatabase(restore);
        });
    }
}
}
