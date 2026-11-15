#include "app/main_window_ui_connection.h"
#include "design_system/metrics/metrics.h"
#include "design_system/status_line/status_line.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolBar>

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

ConnectionToolbar buildConnectionToolbar(QToolBar* toolbar) {
    auto* safety = addSelect(toolbar, "safetyLevel", tr("Safety level"),
                             tr("Safety level for this connection"));
    auto* rowLimit =
        addSelect(toolbar, "rowLimit", tr("Row limit for SELECT"), tr("Row limit for SELECT"));
    return {safety, rowLimit};
}

ResultFooterRow buildResultFooterRow(design::StatusLine* footer) {
    auto* row = new QWidget;
    row->setObjectName("sqlResultFooterRow");
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(design::spacing(design::Spacing::Two), 0, 0, 0);
    layout->setSpacing(design::spacing(design::Spacing::Two));
    auto* tag = new QLabel(row);
    tag->setObjectName("connectionTag");
    tag->setProperty("designRole", "badge");
    tag->hide();
    layout->addWidget(tag);
    layout->addWidget(footer, 1);
    return {row, tag};
}
}
