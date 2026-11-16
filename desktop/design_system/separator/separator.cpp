#include "design_system/separator/separator.h"
#include "design_system/metrics/metrics.h"
#include <QFrame>
#include <QStyle>

namespace choscordb::design {
QFrame* createToolbarSeparator(QWidget* parent) {
    auto* separator = new QFrame(parent);
    separator->setObjectName("toolbarSeparator");
    separator->setFrameShape(QFrame::VLine);
    separator->setFixedHeight(dimension(Dimension::ControlSmall));
    return separator;
}

QFrame* createToneLine(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::NoFrame);
    line->setProperty("designRole", "toneLine");
    line->hide();
    return line;
}

void setToneLine(QFrame* line, std::optional<StatusTone> tone) {
    const auto variant = [tone]() -> QString {
        if (!tone)
            return {};
        switch (*tone) {
        case StatusTone::Neutral:
            return QStringLiteral("neutral");
        case StatusTone::Brand:
            return QStringLiteral("brand");
        case StatusTone::Success:
            return QStringLiteral("success");
        case StatusTone::Warning:
            return QStringLiteral("warning");
        case StatusTone::Danger:
            return QStringLiteral("danger");
        }
        return {};
    }();
    line->setVisible(tone.has_value());
    if (line->property("variant").toString() == variant)
        return;
    line->setProperty("variant", variant);
    line->style()->unpolish(line);
    line->style()->polish(line);
}
}
