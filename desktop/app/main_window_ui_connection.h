#pragma once

class QComboBox;
class QLabel;
class QToolBar;
class QWidget;

namespace choscordb {
namespace design {
class StatusLine;
}

struct ConnectionToolbar {
    QComboBox* safetyLevel;
    QComboBox* rowLimit;
};

ConnectionToolbar buildConnectionToolbar(QToolBar* toolbar);

struct ResultFooterRow {
    QWidget* row;
    QLabel* connectionTag;
};

ResultFooterRow buildResultFooterRow(design::StatusLine* footer);
}
