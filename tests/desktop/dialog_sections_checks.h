#pragma once
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/text/text.h"
#include <QDialog>
#include <QLayout>
#include <QString>
#include <QStringList>

inline QString layoutSpacing(const QLayout* layout) {
    if (!layout)
        return QStringLiteral("none");
    const auto margins = layout->contentsMargins();
    return QStringLiteral("%1,%2,%3,%4/%5")
        .arg(margins.left())
        .arg(margins.top())
        .arg(margins.right())
        .arg(margins.bottom())
        .arg(layout->spacing());
}

// The dialog root, the sections' own layout and the header/footer bars; bodies
// may set their own content inset and spacing.
inline QString sectionedSpacing(const QDialog& dialog) {
    const auto* sections = dialog.findChild<choscordb::design::DialogSections*>();
    if (!sections)
        return QStringLiteral("no DialogSections");
    return QStringLiteral("root=%1 sections=%2 header=%3 footer=%4")
        .arg(layoutSpacing(dialog.layout()), layoutSpacing(sections->layout()),
             layoutSpacing(sections->headerLayout()), layoutSpacing(sections->footerLayout()));
}

inline QString compactSectionedSpacing() {
    const choscordb::design::DialogSections reference;
    return QStringLiteral("root=0,0,0,0/0 sections=%1 header=%2 footer=%3")
        .arg(layoutSpacing(reference.layout()), layoutSpacing(reference.headerLayout()),
             layoutSpacing(reference.footerLayout()));
}

// The header's Headline title, or empty when the header has none.
inline QString headerHeadline(const QDialog& dialog) {
    const auto* sections = dialog.findChild<choscordb::design::DialogSections*>();
    if (!sections)
        return {};
    const auto headline =
        choscordb::design::resolveTypography(choscordb::design::TextStyle::Headline);
    for (const auto* text :
         sections->headerLayout()->parentWidget()->findChildren<choscordb::design::Text*>())
        if (text->font().pixelSize() == headline.pixelSize() &&
            text->font().weight() == headline.weight())
            return text->text();
    return {};
}

// The header dismiss shared by sectioned dialogs, or null when it differs. It must
// never take initial focus or become the default button that Enter triggers.
inline choscordb::design::Button* headerDismiss(const QDialog& dialog, const char* name) {
    const auto* sections = dialog.findChild<choscordb::design::DialogSections*>();
    auto* dismiss = dialog.findChild<choscordb::design::Button*>(name);
    if (!sections || !dismiss || !sections->headerLayout()->parentWidget()->isAncestorOf(dismiss))
        return nullptr;
    const bool shared = dismiss->variant() == choscordb::design::ButtonVariant::Ghost &&
                        dismiss->buttonSize() == choscordb::design::ButtonSize::IconSmall &&
                        dismiss->text().isEmpty() && !dismiss->icon().isNull() &&
                        !dismiss->accessibleName().isEmpty() && !dismiss->autoDefault() &&
                        !dismiss->isDefault() && !(dismiss->focusPolicy() & Qt::TabFocus);
    return shared ? dismiss : nullptr;
}

// Screen-set footer surfaces and non-default footer button sizes.
inline QString footerOverrides(const QDialog& dialog) {
    const auto* sections = dialog.findChild<choscordb::design::DialogSections*>();
    if (!sections)
        return QStringLiteral("no DialogSections");
    QStringList overrides;
    auto* footer = sections->footerLayout()->parentWidget();
    if (footer->property("designSurface").isValid())
        overrides << QStringLiteral("footer designSurface");
    for (const auto* button : footer->findChildren<choscordb::design::Button*>())
        if (button->buttonSize() != choscordb::design::ButtonSize::Default)
            overrides << button->objectName();
    return overrides.join(QLatin1Char(' '));
}
