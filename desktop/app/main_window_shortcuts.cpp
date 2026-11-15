#include "app/editor_preferences.h"
#include "app/main_window.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "bridge/text_filter.h"
#include "design_system/quick_search/quick_search_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QMenu>
#include <QTabWidget>

namespace choscordb {
void MainWindow::addNavigationShortcuts(QMenu* viewMenu) {
    viewMenu->addSeparator();
    const auto add = [this, viewMenu](const QString& id, const QString& text,
                                      const QKeySequence& shortcut, auto handler) {
        auto* action = viewMenu->addAction(text);
        action->setShortcut(shortcut);
        preferences_->addAction(id, action);
        connect(action, &QAction::triggered, this, [this, handler] {
            if (workspace_ && editors_)
                handler();
        });
        return action;
    };
    const auto explorer = [this] {
        return qobject_cast<ObjectExplorer*>(editors_->currentWidget());
    };
    add("switch_connection", tr("Switch connection…"), QKeySequence("Ctrl+Shift+K"),
        [this] { showConnectionPicker(); });
    add("reload_tab", tr("Reload tab"), QKeySequence("Ctrl+R"), [this, explorer] {
        if (auto* object = explorer()) {
            if (const auto reason = object->reload(); !reason.isEmpty())
                reportNotice(reason);
            return;
        }
        reportNotice(tr("Run the query again to reload its results."));
    });
    add("previous_pane", tr("Previous object pane"), QKeySequence("Ctrl+Alt+["), [explorer] {
        if (auto* object = explorer())
            object->stepPane(-1);
    });
    add("next_pane", tr("Next object pane"), QKeySequence("Ctrl+Alt+]"), [explorer] {
        if (auto* object = explorer())
            object->stepPane(1);
    });
    add("previous_page", tr("Previous result page"), QKeySequence("Ctrl+Alt+Left"),
        [this] { workspace_->stepResultPage(false); });
    add("next_page", tr("Next result page"), QKeySequence("Ctrl+Alt+Right"),
        [this] { workspace_->stepResultPage(true); });
    add("export_result", tr("Export result…"), QKeySequence("Ctrl+Shift+E"),
        [this] { workspace_->exportResult(); });
    auto* results = add("toggle_results", tr("Show results"), QKeySequence("Ctrl+`"), [this] {
        resultsHidden_ = !resultsHidden_;
        if (sqlResultArea_)
            sqlResultArea_->setVisible(qobject_cast<SqlEditor*>(editors_->currentWidget()) &&
                                       !resultsHidden_);
    });
    results->setObjectName("toggleResults");
    results->setCheckable(true);
    results->setChecked(true);
    for (int number = 1; number <= 9; ++number)
        add(QStringLiteral("select_tab_%1").arg(number), tr("Go to tab %1").arg(number),
            QKeySequence(QStringLiteral("Ctrl+%1").arg(number)), [this, number] {
                if (number <= editors_->count())
                    editors_->setCurrentIndex(number - 1);
            });
}
QKeySequence MainWindow::replaceShortcut() {
    return QKeySequence::keyBindings(QKeySequence::Replace).isEmpty() ? QKeySequence()
                                                                      : QKeySequence("Ctrl+H");
}
void MainWindow::showConnectionPicker() {
    auto* editor = qobject_cast<SqlEditor*>(editors_->currentWidget());
    if (!editor) {
        reportNotice(tr("Open a query tab before you switch its connection."));
        return;
    }
    if (!connectionPicker_) {
        connectionPicker_ = new design::QuickSearchDialog(this);
        connectionPicker_->setObjectName("connectionPicker");
        connect(connectionPicker_, &design::QuickSearchDialog::queryChanged, this,
                &MainWindow::renderConnectionPicker);
        connect(connectionPicker_, &design::QuickSearchDialog::activated, this,
                [this](const QString& id) {
                    connectionPicker_->close();
                    if (auto* target = qobject_cast<SqlEditor*>(editors_->currentWidget()))
                        workspace_->changeDocumentConnection(target, id.toULongLong());
                });
    }
    renderConnectionPicker();
    connectionPicker_->openSearch();
}
void MainWindow::renderConnectionPicker() {
    const TextFilter filter(connectionPicker_->query().trimmed());
    const auto* current = qobject_cast<SqlEditor*>(editors_->currentWidget());
    const auto& live = workspace_->liveConnections();
    QList<design::QuickSearchResult> rows;
    for (const auto& connection : live)
        if (filter.matches(connection.label))
            rows.append({tr("Connection"), connection.label,
                         current && current->connectionTarget() == connection.id ? tr("Current")
                                                                                 : QString{},
                         QString::number(connection.id), design::Icon::Database});
    connectionPicker_->setResults(rows);
    connectionPicker_->setStatus(rows.isEmpty() ? live.isEmpty() ? tr("No open connections.")
                                                                 : tr("No matching connections.")
                                                : QString{});
}
}
