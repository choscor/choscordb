#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "bridge/request_token.h"
#include "bridge/rust_text.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/right_sheet/right_sheet.h"
#include "design_system/status_line/status_line.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <Qsci/qsciscintilla.h>

namespace choscordb {
using bridge_detail::fromRust;
void QueryWorkspace::explainStatement() {
    const auto connection = selectedConnection();
    auto* editor = widgets_.currentEditor();
    if (!connection || !editor || !connectionAvailable(*connection))
        return;
    const auto sql = editor->text();
    const auto driver = driverForConnection(*connection);
    const auto range = EngineAdapter::executionRange(
        sql, static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS)),
        static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONSTART)),
        static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONEND)),
        RunScope::Current, driver, connectionSqlModes_.value(*connection));
    const auto bytes = sql.toUtf8();
    const auto statement =
        range.valid ? QString::fromUtf8(bytes.mid(static_cast<qsizetype>(range.start),
                                                  static_cast<qsizetype>(range.end - range.start)))
                    : QString();
    if (!planSheet_) {
        planSheet_ = new design::RightSheet(widgets_.dialogParent);
        planSheet_->setObjectName("queryPlanSheet");
        planSheet_->setTitle(tr("Query plan"));
        planTree_ = new QTreeWidget(planSheet_);
        planTree_->setObjectName("queryPlanTree");
        planTree_->setAccessibleName(tr("Query plan"));
        planTree_->setHeaderLabels(
            {tr("Step"), tr("Object"), tr("Cost"), tr("Rows"), tr("Detail")});
        planTree_->header()->setStretchLastSection(true);
        planTree_->setUniformRowHeights(true);
        planSheet_->setBody(planTree_);
        planStatus_ = planSheet_->sections()->addStatusLine();
        planStatus_->setObjectName("queryPlanStatusLine");
        planStatus_->findChild<QLabel*>("statusMessage")->setObjectName("queryPlanStatus");
        auto* close =
            planSheet_->sections()->addAction(design::DialogActionRole::Dismiss, tr("Close"));
        close->setObjectName("queryPlanClose");
        connect(close, &QPushButton::clicked, planSheet_, &QDialog::reject);
        connect(planSheet_, &QDialog::finished, this, [this] { explainToken_ = 0; });
    }
    planTree_->clear();
    explainToken_ = nextRequestToken();
    explainConnection_ = *connection;
    QString error;
    if (!adapter_->explain(*connection, driver, statement, explainToken_, &error)) {
        explainToken_ = 0;
        planStatus_->setBusy(false);
        planStatus_->showError(error);
    } else {
        planStatus_->showInfo(tr("Loading the query plan…"));
        planStatus_->setBusy(true);
    }
    planSheet_->open();
}
void QueryWorkspace::handleExplainEvent(const BridgeEvent& event) {
    if (explainToken_ == 0 || event.id != explainConnection_ || !planSheet_)
        return;
    const auto kind = fromRust(event.kind);
    if (kind != "disconnected" && (event.request_token != explainToken_ ||
                                   (kind != "explain_ready" && kind != "explain_failed")))
        return;
    explainToken_ = 0;
    planStatus_->setBusy(false);
    if (kind != "explain_ready") {
        planStatus_->showError(kind == "disconnected" ? tr("The connection closed.")
                                                      : fromRust(event.error));
        return;
    }
    QList<QTreeWidgetItem*> parents;
    for (const auto& node : event.plan) {
        const auto depth = static_cast<qsizetype>(node.depth);
        parents.resize(std::min(parents.size(), depth));
        QStringList columns{fromRust(node.operation), fromRust(node.target), fromRust(node.cost),
                            fromRust(node.rows), fromRust(node.detail)};
        auto* item = parents.isEmpty() ? new QTreeWidgetItem(planTree_, columns)
                                       : new QTreeWidgetItem(parents.last(), columns);
        if (!parents.isEmpty())
            parents.last()->setExpanded(true);
        parents.push_back(item);
    }
    planStatus_->showInfo(
        tr("%n step(s). The statement did not run.", nullptr, static_cast<int>(event.plan.size())));
}
}
