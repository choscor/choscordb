#pragma once

#include <QString>

class QWidget;

namespace choscordb::design {

class Text;

enum class PanelStateKind { Hidden, Empty, NoMatch, Failed };

Text* createPanelStateText(const QString& text, QWidget* parent);
void showPanelState(Text* label, PanelStateKind kind, const QString& text);

}
