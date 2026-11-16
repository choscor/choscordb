#include "app/main_window_ui_connection.h"
#include "design_system/button/button.h"
#include "design_system/metrics/metrics.h"
#include "design_system/separator/separator.h"
#include "design_system/status_line/status_line.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolBar>
#include <QVBoxLayout>

namespace choscordb {
namespace {
QComboBox* addSelect(QToolBar* toolbar, const char* name, const QString& label,
                     const QString& tip) {
    auto* select = new QComboBox(toolbar);
    select->setObjectName(QString::fromLatin1(name));
    select->setAccessibleName(label);
    select->setToolTip(tip);
    toolbar->addWidget(select);
    return select;
}
QString tr(const char* text) {
    return QCoreApplication::translate("MainWindow", text);
}
}

ConnectionToolbar buildConnectionToolbar(QToolBar* toolbar, QMenu* queryMenu) {
    auto* safety = new design::Button({}, toolbar);
    safety->setObjectName("safetyLevel");
    safety->setAccessibleName(tr("Safety level"));
    safety->setToolTip(tr("Safety level for this connection"));
    safety->setVariant(design::ButtonVariant::Outline);
    safety->setButtonSize(design::ButtonSize::Small);
    toolbar->addWidget(safety);
    auto* database = new design::Button({}, toolbar);
    database->setObjectName("switchDatabaseButton");
    database->setAccessibleName(tr("Switch database…"));
    database->setDesignIcon(design::Icon::Database);
    database->setVariant(design::ButtonVariant::Outline);
    database->setButtonSize(design::ButtonSize::IconSmall);
    toolbar->addWidget(database);
    auto* rowLimit =
        addSelect(toolbar, "rowLimit", tr("Row limit for SELECT"), tr("Row limit for SELECT"));
    auto* explain = queryMenu->addAction(tr("Explain"));
    explain->setObjectName("explainStatement");
    explain->setShortcut(QKeySequence("Ctrl+Alt+E"));
    explain->setEnabled(false);
    auto* button = new design::Button(explain->text(), toolbar);
    button->setObjectName("explainStatementButton");
    button->setVariant(design::ButtonVariant::Outline);
    button->setButtonSize(design::ButtonSize::Small);
    button->setEnabled(false);
    toolbar->addWidget(button);
    QObject::connect(button, &QPushButton::clicked, explain, &QAction::trigger);
    QObject::connect(explain, &QAction::changed, button, [explain, button] {
        button->setEnabled(explain->isEnabled());
        button->setToolTip(tr("Show the query plan of the statement at the cursor") + " · " +
                           explain->shortcut().toString(QKeySequence::NativeText));
    });
    return {safety, database, rowLimit, explain};
}

ResultFooterRow buildResultFooterRow(design::StatusLine* footer) {
    auto* row = new QWidget;
    row->setObjectName("sqlResultFooterRow");
    auto* column = new QVBoxLayout(row);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    auto* line = design::createToneLine(row);
    line->setObjectName("connectionToneLine");
    column->addWidget(line);
    auto* layout = new QHBoxLayout;
    layout->setContentsMargins(design::spacing(design::Spacing::Two), 0, 0, 0);
    layout->setSpacing(design::spacing(design::Spacing::Two));
    auto* tag = new QLabel(row);
    tag->setObjectName("connectionTag");
    tag->setProperty("designRole", "badge");
    tag->hide();
    layout->addWidget(tag);
    layout->addWidget(footer, 1);
    column->addLayout(layout);
    return {row, tag, line};
}
}
