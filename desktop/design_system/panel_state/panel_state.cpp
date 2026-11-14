#include "design_system/panel_state/panel_state.h"

#include "design_system/text/text.h"

#include <QStyle>

namespace choscordb::design {

Text* createPanelStateText(const QString& text, QWidget* parent) {
    auto* label = new Text(text, parent);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    label->setProperty("panelState", true);
    label->setTextFormat(Qt::PlainText);
    showPanelState(label, text.isEmpty() ? PanelStateKind::Hidden : PanelStateKind::Empty, text);
    return label;
}

void showPanelState(Text* label, PanelStateKind kind, const QString& text) {
    const bool failed = kind == PanelStateKind::Failed;
    label->setProperty("designRole", failed ? "fieldError" : "description");
    label->setProperty("state", failed ? "error" : "");
    label->setText(text);
    label->setAccessibleName(text);
    label->style()->unpolish(label);
    label->style()->polish(label);
    label->setVisible(kind != PanelStateKind::Hidden && !text.isEmpty());
}

}
