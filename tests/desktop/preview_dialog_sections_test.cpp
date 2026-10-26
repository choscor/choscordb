#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/theme.h"
#include "dialog_sections_checks.h"
#include "preview_test.h"
#include "preview_test_helpers.h"
#include "tools/preview/preview_window.h"
#include <QCheckBox>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtTest>

void PreviewTest::dialogSectionsPreviewUsesRealComponentInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("dialog-sections"));
    window.show();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* open = host->findChild<QPushButton*>("previewOpenDialogSections");
        auto* dialog = previewSurface<QDialog>(host, "previewOpenDialogSections");
        auto* sections =
            dialog->findChild<choscordb::design::DialogSections*>("previewDialogSections");
        QVERIFY(open && dialog && sections);
        auto* dismiss =
            sections->findChild<choscordb::design::Button*>("previewDialogSectionsDismiss");
        QVERIFY(dismiss);
        QCOMPARE(dismiss->text(), QString());
        QCOMPARE(dismiss->buttonSize(), choscordb::design::ButtonSize::IconSmall);
        QVERIFY(!dismiss->icon().isNull());
        open->click();
        QTRY_VERIFY(dialog->isVisible());
        QCOMPARE(sections->geometry(), dialog->rect());
        QCOMPARE(sections->bodyLayout()->contentsMargins().left(),
                 choscordb::design::spacing(choscordb::design::Spacing::Three));
        QCOMPARE(sections->bodyLayout()->contentsMargins().right(),
                 choscordb::design::spacing(choscordb::design::Spacing::Three));
        QVERIFY(sections->headerLayout()->parentWidget()->height() < 66);
        QVERIFY(sections->footerLayout()->parentWidget()->height() < 70);
        QVERIFY(sections->bodyLayout()->parentWidget()->height() > 0);
        auto* hostField = dialog->findChild<QLineEdit*>("previewProfileHost");
        auto* port = dialog->findChild<QSpinBox*>("previewProfilePort");
        auto* database = dialog->findChild<QLineEdit*>("previewProfileDatabase");
        auto* username = dialog->findChild<QLineEdit*>("previewProfileUsername");
        auto* password = dialog->findChild<QLineEdit*>("previewProfilePassword");
        QVERIFY(hostField && port && database && username && password);
        const auto rowCenter = [](QWidget* control) { return control->geometry().center().y(); };
        QVERIFY(qAbs(rowCenter(hostField) - rowCenter(port)) <= 4);
        QVERIFY(qAbs(rowCenter(hostField) - rowCenter(database)) <= 4);
        QVERIFY(qAbs(rowCenter(username) - rowCenter(password)) <= 4);
        QVERIFY(port->width() < hostField->width());
        QCOMPARE(password->echoMode(), QLineEdit::Password);
        auto* saveCredentials = dialog->findChild<QCheckBox*>("previewProfileSaveCredentials");
        QVERIFY(saveCredentials);
        QVERIFY(!dialog->findChild<QCheckBox*>("previewProfileUseTls"));
        dialog->reject();
    }
}

void PreviewTest::dialogSectionsHaveMutedPaddedFooterInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("dialog-sections"));
    window.show();
    for (const auto* theme : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(theme);
        auto* open = host->findChild<QPushButton*>("previewOpenDialogSections");
        auto* dialog = previewSurface<QDialog>(host, "previewOpenDialogSections");
        QVERIFY(open && dialog);
        open->click();
        QTRY_VERIFY(dialog->isVisible());
        auto* sections = dialog->findChild<choscordb::design::DialogSections*>();
        QVERIFY(sections);
        const auto headerMargins = sections->headerLayout()->contentsMargins();
        const auto footerMargins = sections->footerLayout()->contentsMargins();
        QVERIFY(headerMargins.right() <= 8);
        QVERIFY(footerMargins.top() >= 10);
        QVERIFY(footerMargins.bottom() >= 10);
        auto* footer = sections->footerLayout()->parentWidget();
        auto* body = sections->bodyLayout()->parentWidget();
        const auto footerColor = footer->grab().toImage().pixelColor(20, footer->height() / 2);
        const auto bodyColor = body->grab().toImage().pixelColor(20, body->height() - 10);
        QVERIFY(footerColor != bodyColor);
        QCOMPARE(footerColor,
                 choscordb::design::resolvedThemeForWidget(*footer).colors.surfaceRaised);
        dialog->reject();
    }
}

void PreviewTest::dialogSectionsHideEmptyFooterInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("dialog-sections"));
    window.show();
    for (const auto* theme : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(theme);
        auto* open = host->findChild<QPushButton*>("previewOpenDialogSectionsProgress");
        auto* dialog = previewSurface<QDialog>(host, "previewOpenDialogSectionsProgress");
        QVERIFY(open && dialog);
        auto* sections =
            dialog->findChild<choscordb::design::DialogSections*>("previewDialogSectionsProgress");
        QVERIFY(sections);
        QVERIFY(sections->findChild<QProgressBar*>());
        open->click();
        QTRY_VERIFY(dialog->isVisible());
        auto* separator = sections->findChild<QFrame*>("dialogSectionsFooterSeparator");
        QVERIFY(separator);
        QVERIFY(!sections->footerLayout()->parentWidget()->isVisible());
        QVERIFY(!separator->isVisible());
        QCOMPARE(sections->bodyLayout()->parentWidget()->geometry().bottom(),
                 sections->rect().bottom());
        dialog->reject();
    }
}

void PreviewTest::dialogSpecimensUseSharedSections_data() {
    QTest::addColumn<QString>("specimen");
    QTest::newRow("modal") << "dialogs";
    QTest::newRow("nonmodal") << "nonmodal";
}

void PreviewTest::dialogSpecimensUseSharedSections() {
    QFETCH(QString, specimen);
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen(specimen));
    window.show();
    for (const auto* theme : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(theme);
        auto* open = host->findChild<QPushButton*>("previewOpenDialog");
        auto* dialog = previewSurface<QDialog>(host);
        QVERIFY(open && dialog);
        open->click();
        QTRY_VERIFY(dialog->isVisible());
        QCOMPARE(sectionedSpacing(*dialog), compactSectionedSpacing());
        QCOMPARE(headerHeadline(*dialog), QString("Panel details"));
        auto* dismiss = headerDismiss(*dialog, "previewDialogDismiss");
        QVERIFY(dismiss);
        dismiss->click();
        QVERIFY(!dialog->isVisible());
    }
}
