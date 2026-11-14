#include "design_system/kbd/key_hints.h"

#include "design_system/fonts/fonts.h"
#include "design_system/metrics/metrics.h"
#include "design_system/text/text.h"

#include <QHBoxLayout>
#include <QStringList>

namespace choscordb::design {

KeyHints::KeyHints(const QList<KeyHint>& hints, QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(spacing(Spacing::One));
    QStringList spoken;
    for (const auto& hint : hints) {
        if (layout->count() > 0)
            layout->addSpacing(spacing(Spacing::Two));
        auto* key = new QLabel(hint.keys, this);
        key->setProperty("designRole", "kbd");
        key->setFont(resolveTypography(TextStyle::Callout, FontDesign::Monospaced));
        layout->addWidget(key);
        auto* action = new Text(hint.action, this);
        action->setTextStyle(TextStyle::Caption);
        action->setProperty("designRole", "description");
        layout->addWidget(action);
        spoken << QStringLiteral("%1 %2").arg(hint.keys, hint.action);
    }
    layout->addStretch(1);
    setAccessibleName(tr("Keyboard shortcuts"));
    setAccessibleDescription(spoken.join(QStringLiteral(", ")));
}

}
