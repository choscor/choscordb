#include "app/result_pane_tabs.h"

#include <QCoreApplication>
#include <QStackedWidget>
#include <QTabBar>

namespace choscordb {
QTabBar* createResultPaneTabs(QStackedWidget* panes, const QString& objectName) {
    auto* tabs = new QTabBar;
    tabs->setObjectName(objectName);
    tabs->setAccessibleName(QCoreApplication::translate("ResultPaneTabs", "Result panes"));
    tabs->setExpanding(false);
    tabs->addTab(QCoreApplication::translate("ResultPaneTabs", "Results"));
    tabs->addTab(QCoreApplication::translate("ResultPaneTabs", "Messages"));
    tabs->setCurrentIndex(panes->currentIndex());
    QObject::connect(tabs, &QTabBar::currentChanged, panes, &QStackedWidget::setCurrentIndex);
    QObject::connect(panes, &QStackedWidget::currentChanged, tabs, &QTabBar::setCurrentIndex);
    return tabs;
}
}
