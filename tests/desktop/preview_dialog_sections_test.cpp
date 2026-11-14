#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/spinner/spinner.h"
#include "design_system/status_line/status_line.h"
#include "design_system/theme.h"
#include "dialog_sections_checks.h"
#include "preview_test.h"
#include "preview_test_helpers.h"
#include "tools/preview/preview_window.h"
#include <QCheckBox>
#include <QDialog>
#include <QFormLayout>
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
                 choscordb::design::spacing(choscordb::design::Spacing::Six));
        QCOMPARE(sections->bodyLayout()->contentsMargins().right(),
                 choscordb::design::spacing(choscordb::design::Spacing::Six));
        QVERIFY(sections->headerLayout()->parentWidget()->height() < 66);
        QVERIFY(sections->findChild<QWidget*>("dialogSectionsFooter")->height() < 70);
        QVERIFY(sections->bodyLayout()->parentWidget()->height() > 0);
        auto* hostField = dialog->findChild<QLineEdit*>("previewProfileHost");
        auto* port = dialog->findChild<QSpinBox*>("previewProfilePort");
        auto* database = dialog->findChild<QLineEdit*>("previewProfileDatabase");
        auto* username = dialog->findChild<QLineEdit*>("previewProfileUsername");
        auto* password = dialog->findChild<QLineEdit*>("previewProfilePassword");
        QVERIFY(hostField && port && database && username && password);
        auto* form = sections->findChild<QFormLayout*>();
        QVERIFY(form);
        for (QWidget* field : QList<QWidget*>{hostField, port, database, username, password}) {
            auto* label = qobject_cast<QLabel*>(form->labelForField(field));
            QVERIFY(label);
            QCOMPARE(label->buddy(), field);
            QVERIFY(label->geometry().right() < field->geometry().left());
            QVERIFY(qAbs(label->geometry().center().y() - field->geometry().center().y()) <= 2);
            QCOMPARE(field->geometry().left(), hostField->geometry().left());
        }
        QCOMPARE(qobject_cast<QLabel*>(form->labelForField(database))->text(),
                 QString("&Database (optional)"));
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
        const auto footerMargins =
            sections->findChild<QWidget*>("dialogSectionsFooter")->layout()->contentsMargins();
        QCOMPARE(headerMargins.right(),
                 choscordb::design::spacing(choscordb::design::Spacing::Six));
        QCOMPARE(headerMargins.top(), headerMargins.bottom());
        QCOMPARE(headerMargins.top(), choscordb::design::spacing(choscordb::design::Spacing::Four));
        QVERIFY(footerMargins.top() >= 10);
        QVERIFY(footerMargins.bottom() >= 10);
        auto* footer = sections->findChild<QWidget*>("dialogSectionsFooter");
        auto* body = sections->bodyLayout()->parentWidget();
        const auto footerColor = footer->grab().toImage().pixelColor(20, footerMargins.top() / 2);
        const auto bodyColor = body->grab().toImage().pixelColor(20, body->height() - 10);
        QVERIFY(footerColor != bodyColor);
        QCOMPARE(footerColor, choscordb::design::resolvedThemeForWidget(*footer).colors.elevated);
        dialog->reject();
    }
}

void PreviewTest::dialogSectionsShowBusyStateInTheStatusLineInBothThemes() {
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
        QVERIFY(!sections->findChild<QProgressBar*>());
        open->click();
        QTRY_VERIFY(dialog->isVisible());
        auto* status =
            sections->findChild<choscordb::design::StatusLine*>("previewDialogSectionsStatus");
        QVERIFY(status && status->isVisible());
        auto* spinner = status->findChild<choscordb::design::Spinner*>();
        QVERIFY(spinner && spinner->isVisible());
        auto* footer = sections->findChild<QWidget*>("dialogSectionsFooter");
        QVERIFY(footer->isVisible());
        QCOMPARE(footer->findChildren<choscordb::design::Button*>().size(), 1);
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
