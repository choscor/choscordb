#pragma once

class QAction;
class QComboBox;
class QFrame;
class QLabel;
class QMenu;
class QToolBar;
class QWidget;

namespace choscordb {
namespace design {
class Button;
class StatusLine;
}

struct ConnectionToolbar {
    design::Button* safetyLevel;
    design::Button* switchDatabase;
    QComboBox* rowLimit;
    QAction* explain;
};

ConnectionToolbar buildConnectionToolbar(QToolBar* toolbar, QMenu* queryMenu);

struct ResultFooterRow {
    QWidget* row;
    QLabel* connectionTag;
    QFrame* connectionToneLine;
};

ResultFooterRow buildResultFooterRow(design::StatusLine* footer);
}
