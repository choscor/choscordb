#pragma once

class QAction;
class QComboBox;
class QLabel;
class QMenu;
class QToolBar;
class QWidget;

namespace choscordb {
namespace design {
class StatusLine;
}

struct ConnectionToolbar {
    QComboBox* safetyLevel;
    QComboBox* rowLimit;
    QAction* explain;
};

ConnectionToolbar buildConnectionToolbar(QToolBar* toolbar, QMenu* queryMenu);

struct ResultFooterRow {
    QWidget* row;
    QLabel* connectionTag;
};

ResultFooterRow buildResultFooterRow(design::StatusLine* footer);
}
