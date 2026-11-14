#pragma once

class QStackedWidget;
class QTabBar;
class QString;

namespace choscordb {
QTabBar* createResultPaneTabs(QStackedWidget* panes, const QString& objectName);
}
