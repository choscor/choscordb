#include "control_style_test.h"
#include "design_system/colors/colors.h"
#include "design_system/panel_state/panel_state.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
#include <QLineEdit>
#include <QVBoxLayout>
#include <QtTest>
#include <algorithm>

namespace {
QImage rendered(QWidget& widget) {
    return widget.window()->grab(widget.geometry()).toImage();
}
double textContrast(const QImage& withText, const QImage& withoutText) {
    double best = 1.0;
    for (int y = 0; y < withText.height(); ++y)
        for (int x = 0; x < withText.width(); ++x)
            if (withText.pixel(x, y) != withoutText.pixel(x, y))
                best = std::max(best, choscordb::design::contrastRatio(
                                          withText.pixelColor(x, y), withoutText.pixelColor(x, y)));
    return best;
}
}

void ControlStyleTest::renderedPanelStateKeepsMutedContrastAndPlaceholderUsesKumo_data() {
    QTest::addColumn<choscordb::design::ThemeMode>("mode");
    QTest::newRow("light") << choscordb::design::ThemeMode::Light;
    QTest::newRow("dark") << choscordb::design::ThemeMode::Dark;
}

void ControlStyleTest::renderedPanelStateKeepsMutedContrastAndPlaceholderUsesKumo() {
    using namespace choscordb::design;
    QFETCH(ThemeMode, mode);
    QWidget root;
    ThemeManager theme;
    theme.setMode(mode);
    theme.applyTo(root);
    auto* layout = new QVBoxLayout(&root);
    auto* panel = createPanelStateText("No saved queries yet.", &root);
    layout->addWidget(panel);
    auto* field = new QLineEdit(&root);
    field->setPlaceholderText("Filter objects");
    layout->addWidget(field);
    root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    field->clearFocus();
    QVERIFY(contrastRatio(theme.resolvedTheme().colors.textSubtle,
                          theme.resolvedTheme().colors.base) >= 4.5);
    const auto panelText = rendered(*panel);
    panel->setText({});
    const auto panelContrast = textContrast(panelText, rendered(*panel));
    QVERIFY2(panelContrast >= 4.5, qPrintable(QString::number(panelContrast)));
    const auto placeholderText = rendered(*field);
    field->setPlaceholderText({});
    const auto placeholderContrast = textContrast(placeholderText, rendered(*field));
    const auto& colors = theme.resolvedTheme().colors;
    const auto kumoContrast = contrastRatio(colors.textPlaceholder, colors.control);
    QVERIFY2(qAbs(placeholderContrast - kumoContrast) < 0.2,
             qPrintable(QString::number(placeholderContrast)));
}
