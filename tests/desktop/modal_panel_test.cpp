#include "design_system/modal_panel.h"
#include "design_system/theme_manager.h"
#include "widgets/confirmation_dialog.h"
#include "widgets/dialog_shell.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTimer>
#include <QVBoxLayout>
#include <QtTest>

class ModalPanelTest final : public QObject {
    Q_OBJECT
  private slots:
    void shortConfirmationDoesNotClipBodyOrFooter() {
        QWidget parent;
        parent.resize(960, 640);
        choscordb::design::ThemeManager theme;
        theme.setMode(choscordb::design::ThemeMode::Light);
        theme.installOn(qApp);
        theme.applyTo(parent);
        parent.show();
        choscordb::ConfirmationDialog dialog(
            QMessageBox::Warning, "Disconnect database session",
            "Disconnect :memory:? Any uncommitted transaction will be rolled back.",
            QMessageBox::Cancel, &parent);
        auto* destructive = dialog.addButton("Disconnect", QMessageBox::DestructiveRole);
        dialog.setDefaultButton(QMessageBox::Cancel);
        dialog.show();
        QApplication::processEvents();
        auto* footer = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(footer);
        for (auto* button : dialog.buttons()) {
            QCOMPARE(button->visibleRegion(), QRegion(button->rect()));
        }
        auto* scroll = dialog.findChild<QScrollArea*>("confirmationBodyScroll");
        QVERIFY(scroll);
        QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
        const auto rendered = dialog.grab().toImage();
        const QPoint sample(destructive->width() / 2, destructive->height() - 5);
        const QPoint inDialog = destructive->mapTo(&dialog, sample);
        QCOMPARE(rendered.pixelColor(qRound(inDialog.x() * rendered.devicePixelRatio()),
                                     qRound(inDialog.y() * rendered.devicePixelRatio())),
                 QColor("#fde5e6"));
        dialog.reject();
    }
    void longDiagnosticsScrollWhileCancellationStaysVisible() {
        QWidget parent;
        parent.resize(960, 640);
        parent.show();
        QString diagnostic;
        for (int line = 0; line < 100; ++line) {
            diagnostic +=
                QString("Diagnostic %1: synthetic connection recovery failure.\n").arg(line);
        }
        choscordb::ConfirmationDialog dialog(QMessageBox::Warning, "Recovery failed", diagnostic,
                                             QMessageBox::Cancel, &parent);
        dialog.setDefaultButton(QMessageBox::Cancel);
        dialog.show();
        QApplication::processEvents();
        const QRect ownerRect(parent.mapToGlobal(QPoint()), parent.size());
        QVERIFY(ownerRect.contains(dialog.geometry()));
        auto* cancel = dialog.button(QMessageBox::Cancel);
        QVERIFY(cancel && cancel->isVisible());
        QCOMPARE(cancel->visibleRegion(), QRegion(cancel->rect()));
        QVERIFY(ownerRect.contains(QRect(cancel->mapToGlobal(QPoint()), cancel->size())));
        QCOMPARE(dialog.text(), diagnostic);
        auto* scroll = dialog.findChild<QScrollArea*>("confirmationBodyScroll");
        QVERIFY(scroll);
        auto* body = scroll->findChild<QLabel*>("confirmationBodyText");
        QVERIFY(body);
        QCOMPARE(body->text(), diagnostic);
        QCOMPARE(body->accessibleName(), diagnostic);
        QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QCOMPARE(scroll->verticalScrollBar()->value(), scroll->verticalScrollBar()->maximum());
        QTest::mouseClick(cancel, Qt::LeftButton);
        QVERIFY(!dialog.isVisible());
    }
    void openConfirmationTracksThemeAndParentGeometry() {
        QWidget parent;
        parent.resize(640, 480);
        choscordb::design::ThemeManager theme;
        theme.setMode(choscordb::design::ThemeMode::Light);
        theme.applyTo(parent);
        parent.show();
        choscordb::ConfirmationDialog dialog(QMessageBox::Question, "Close query",
                                             "Discard changes?",
                                             QMessageBox::Discard | QMessageBox::Cancel, &parent);
        dialog.setDefaultButton(QMessageBox::Cancel);
        dialog.show();
        const auto lightIcon = dialog.iconPixmap().toImage();
        theme.setMode(choscordb::design::ThemeMode::Dark);
        theme.applyTo(parent);
        QApplication::processEvents();
        QVERIFY(dialog.iconPixmap().toImage() != lightIcon);
        QCOMPARE(dialog.width(), 384);
        parent.resize(800, 600);
        QApplication::processEvents();
        QCOMPARE(dialog.geometry().center(), parent.mapToGlobal(parent.rect().center()));
        auto* backdrop = parent.findChild<QWidget*>("modalBackdrop");
        QVERIFY(backdrop);
        QCOMPARE(backdrop->geometry(), parent.rect());
        dialog.reject();
    }
    void questionEscapeKeepsCancelResultAndRestoresKeyboardFocus() {
        QWidget parent;
        QVBoxLayout layout(&parent);
        QLineEdit original(&parent);
        layout.addWidget(&original);
        parent.resize(640, 480);
        parent.show();
        original.setFocus();
        QTRY_VERIFY(original.hasFocus());
        bool defaultWasCancel = false;
        bool focusContained = true;
        QTimer::singleShot(50, &parent, [&] {
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!dialog) {
                focusContained = false;
                return;
            }
            defaultWasCancel = dialog->defaultButton() == dialog->button(QMessageBox::Cancel);
            for (int i = 0; i < 6; ++i) {
                QTest::keyClick(dialog, Qt::Key_Tab);
                auto* focused = QApplication::focusWidget();
                focusContained = focusContained && focused &&
                                 (focused == dialog || dialog->isAncestorOf(focused));
            }
            QTest::keyClick(dialog, Qt::Key_Escape);
        });
        const auto result = choscordb::ConfirmationDialog::question(
            &parent, "Discard query", "Discard synthetic changes?",
            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        QCOMPARE(result, QMessageBox::Cancel);
        QVERIFY(defaultWasCancel);
        QVERIFY(focusContained);
        QTRY_VERIFY(original.hasFocus());
    }
    void nonmodalDialogUsesSharedSurfaceWithoutBlockingItsOwner() {
        QWidget parent;
        parent.resize(640, 480);
        choscordb::design::ThemeManager theme;
        theme.setMode(choscordb::design::ThemeMode::Dark);
        theme.applyTo(parent);
        parent.show();
        choscordb::DialogShell dialog(&parent);
        QVBoxLayout content(&dialog);
        content.addWidget(new QLineEdit(&dialog));
        dialog.resize(320, 160);
        dialog.show();
        QVERIFY(!dialog.isModal());
        QVERIFY(!dialog.windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(!parent.findChild<QWidget*>("modalBackdrop"));
        QCOMPARE(content.contentsMargins(), QMargins(16, 16, 16, 16));
        const auto capture = dialog.grab();
        const auto scale = capture.devicePixelRatio();
        QCOMPARE(capture.toImage().pixelColor(qRound(160 * scale), qRound(140 * scale)),
                 QColor("#171717"));
        dialog.close();
    }
    void confirmationPreservesMessageBoxContractInsideDimmedPanel() {
        QWidget parent;
        parent.resize(640, 480);
        parent.show();
        choscordb::ConfirmationDialog dialog(QMessageBox::Warning, "Delete profile",
                                             "Delete the synthetic profile?", QMessageBox::Cancel,
                                             &parent);
        auto* destructive = dialog.addButton("Delete", QMessageBox::DestructiveRole);
        dialog.setDefaultButton(QMessageBox::Cancel);
        dialog.show();
        QVERIFY(qobject_cast<QMessageBox*>(&dialog));
        QCOMPARE(dialog.defaultButton(), dialog.button(QMessageBox::Cancel));
        QCOMPARE(dialog.buttonRole(destructive), QMessageBox::DestructiveRole);
        QVERIFY(dialog.windowFlags().testFlag(Qt::FramelessWindowHint));
        auto* backdrop = parent.findChild<QWidget*>("modalBackdrop");
        QVERIFY(backdrop && backdrop->isVisible());
        QCOMPARE(dialog.geometry().center(), parent.mapToGlobal(parent.rect().center()));
        auto* heading = dialog.findChild<QLabel*>("confirmationHeading");
        QVERIFY(heading && heading->isVisible());
        QCOMPARE(heading->text(), QString("Delete profile"));
        QSignalSpy clicked(destructive, &QPushButton::clicked);
        QTest::mouseClick(backdrop, Qt::LeftButton, {}, QPoint(2, 2));
        QCOMPARE(clicked.count(), 0);
        QVERIFY(!dialog.isVisible());
        QVERIFY(!backdrop->isVisible());
    }
    void panelUsesReferenceWidthAndContentSpacing() {
        QWidget parent;
        parent.resize(640, 480);
        parent.show();
        choscordb::design::ModalPanel panel(&parent);
        QVBoxLayout content(&panel);
        content.addWidget(new QLineEdit(&panel));
        content.addWidget(new QPushButton("Cancel", &panel));
        panel.show();
        QCOMPARE(panel.width(), 384);
        QCOMPARE(content.contentsMargins(), QMargins(16, 16, 16, 16));
        QCOMPARE(content.spacing(), 16);
        panel.reject();
    }
    void panelRendersRoundedPopoverSurface() {
        QWidget parent;
        choscordb::design::ThemeManager theme;
        theme.setMode(choscordb::design::ThemeMode::Dark);
        theme.applyTo(parent);
        choscordb::design::ModalPanel panel(&parent);
        panel.resize(200, 120);
        const auto image = panel.grab().toImage();
        QCOMPARE(image.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(image.pixelColor(100, 60), QColor("#171717"));
    }
    void destroyingAnOpenPanelRemovesItsBackdrop() {
        QWidget parent;
        parent.resize(640, 480);
        parent.show();
        auto* panel = new choscordb::design::ModalPanel(&parent);
        panel->show();
        QPointer<QWidget> backdrop = parent.findChild<QWidget*>("modalBackdrop");
        QVERIFY(backdrop);
        delete panel;
        QVERIFY(backdrop.isNull());
    }
    void escapeRejectsDimmedModalAndRestoresFocus() {
        QWidget parent;
        QVBoxLayout outer(&parent);
        QLineEdit original(&parent);
        outer.addWidget(&original);
        parent.resize(640, 480);
        parent.show();
        original.setFocus();
        QTRY_VERIFY(original.hasFocus());
        choscordb::design::ModalPanel panel(&parent);
        QVBoxLayout content(&panel);
        QLineEdit field(&panel);
        QPushButton accept("Delete", &panel);
        content.addWidget(&field);
        content.addWidget(&accept);
        QObject::connect(&accept, &QPushButton::clicked, &panel, &QDialog::accept);
        QSignalSpy accepted(&panel, &QDialog::accepted);
        QSignalSpy rejected(&panel, &QDialog::rejected);
        panel.show();
        QVERIFY(panel.isModal());
        auto* backdrop = parent.findChild<QWidget*>("modalBackdrop");
        QVERIFY(backdrop);
        QVERIFY(backdrop->isVisible());
        QCOMPARE(backdrop->geometry(), parent.rect());
        field.setFocus();
        QTest::keyClick(&field, Qt::Key_Escape);
        QCOMPARE(accepted.count(), 0);
        QCOMPARE(rejected.count(), 1);
        QVERIFY(!backdrop->isVisible());
        QTRY_VERIFY(original.hasFocus());
    }
};
QTEST_MAIN(ModalPanelTest)
#include "modal_panel_test.moc"
