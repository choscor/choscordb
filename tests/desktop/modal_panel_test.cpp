#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/modal_panel/modal_panel.h"
#include "design_system/theme_manager.h"
#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalSpy>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <QtTest>
#include <memory>

class ModalPanelTest final : public QObject {
    Q_OBJECT
  private slots:
    void modalIsPartOfOwnerWindow_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("panel") << 0;
        QTest::newRow("confirmation") << 1;
        QTest::newRow("dialog-shell") << 2;
    }
    void modalIsPartOfOwnerWindow() {
        QFETCH(int, kind);
        QWidget owner;
        owner.resize(960, 640);
        std::unique_ptr<QDialog> dialog;
        if (kind == 1) {
            dialog = std::make_unique<choscordb::ConfirmationDialog>(
                QMessageBox::Question, "Confirm", "Continue?", QMessageBox::Cancel, &owner);
        } else if (kind == 2) {
            auto shell = std::make_unique<choscordb::DialogShell>(&owner);
            shell->setAppModal();
            auto* layout = new QVBoxLayout(shell.get());
            layout->addWidget(new QLineEdit(shell.get()));
            dialog = std::move(shell);
        } else {
            dialog = std::make_unique<choscordb::design::ModalPanel>(&owner);
            auto* layout = new QVBoxLayout(dialog.get());
            layout->addWidget(new QLineEdit(dialog.get()));
        }
        owner.show();
        QCoreApplication::processEvents();
        QVERIFY(!dialog->isVisible());
        QVERIFY(!choscordb::design::DialogPresentation::activeDialog(&owner));
        dialog->show();
        QCoreApplication::processEvents();
        QVERIFY2(!dialog->isWindow(),
                 "The live modal must be a child surface, not a separate native window");
        QCOMPARE(dialog->window(), &owner);
        QVERIFY(owner.rect().contains(QRect(dialog->mapTo(&owner, QPoint()), dialog->size())));
        owner.resize(320, 260);
        QCoreApplication::processEvents();
        QVERIFY(owner.rect().contains(QRect(dialog->mapTo(&owner, QPoint()), dialog->size())));
        QCOMPARE(dialog->geometry().center(), owner.rect().center());
        owner.move(owner.pos() + QPoint(35, 45));
        QCoreApplication::processEvents();
        QCOMPARE(dialog->geometry().center(), owner.rect().center());
        dialog->reject();
    }
    void embeddedPopupsInsideModalKeepLiveInputAndEscapeScoped() {
        QWidget owner;
        owner.resize(800, 600);
        choscordb::design::ThemeManager theme;
        theme.installOn(qApp);
        theme.applyTo(owner);
        choscordb::design::ModalPanel panel(&owner);
        QVBoxLayout layout(&panel);
        QComboBox combo(&panel);
        combo.addItems({"One", "Two", "Three"});
        QPushButton menuButton("Actions", &panel);
        QMenu menu(&menuButton);
        auto* action = menu.addAction("Run action");
        QSignalSpy triggered(action, &QAction::triggered);
        connect(&menuButton, &QPushButton::clicked, &menu,
                [&] { menu.popup(menuButton.mapToGlobal(QPoint(0, menuButton.height()))); });
        layout.addWidget(&combo);
        layout.addWidget(&menuButton);
        owner.show();
        owner.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&owner));
        QVERIFY(!panel.isVisible());
        QVERIFY(!combo.view()->isVisible());
        QVERIFY(!menu.isVisible());
        panel.open();
        QTRY_VERIFY(panel.isVisible());
        QVERIFY(!combo.view()->isVisible());
        const auto clickCombo = [&] {
            QTest::mouseClick(owner.windowHandle(), Qt::LeftButton, {},
                              combo.mapTo(&owner, QPoint(combo.width() - 10, combo.height() / 2)));
        };
        clickCombo();
        QTRY_VERIFY(combo.view()->isVisible());
        QCOMPARE(combo.view()->parentWidget()->parentWidget(), &owner);
        QVERIFY(!panel.isAncestorOf(combo.view()));
        QTest::keyClick(owner.windowHandle(), Qt::Key_Down);
        QTest::keyClick(owner.windowHandle(), Qt::Key_Return);
        QTRY_COMPARE(combo.currentIndex(), 1);
        QVERIFY(!combo.view()->isVisible());
        QVERIFY(panel.isVisible());
        clickCombo();
        QTRY_VERIFY(combo.view()->isVisible());
        QTest::keyClick(owner.windowHandle(), Qt::Key_Escape);
        QTRY_VERIFY(!combo.view()->isVisible());
        QVERIFY(panel.isVisible());
        QCOMPARE(combo.currentIndex(), 1);
        QTest::mouseClick(owner.windowHandle(), Qt::LeftButton, {},
                          menuButton.mapTo(&owner, menuButton.rect().center()));
        QTRY_VERIFY(menu.isVisible());
        QCOMPARE(menu.parentWidget(), &owner);
        QVERIFY(!panel.isAncestorOf(&menu));
        QTest::mouseClick(owner.windowHandle(), Qt::LeftButton, {},
                          menu.mapTo(&owner, menu.actionGeometry(action).center()));
        QTRY_COMPARE(triggered.count(), 1);
        QVERIFY(!menu.isVisible());
        QVERIFY(panel.isVisible());
        QCOMPARE(choscordb::design::DialogPresentation::activeDialog(&owner), &panel);
        QTest::keyClick(owner.windowHandle(), Qt::Key_Escape);
        QTRY_VERIFY(!panel.isVisible());
    }
    void nestedConfirmationKeepsExecAndOwnerInputContracts() {
        QWidget owner;
        owner.resize(800, 600);
        QLineEdit original(&owner);
        original.setGeometry(20, 20, 180, 30);
        owner.show();
        owner.activateWindow();
        original.setFocus();
        QTRY_VERIFY(original.hasFocus());
        QShortcut ownerShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), &owner);
        QSignalSpy shortcutActivated(&ownerShortcut, &QShortcut::activated);
        choscordb::design::ModalPanel panel(&owner);
        QVBoxLayout layout(&panel);
        QLineEdit field(&panel);
        layout.addWidget(&field);
        panel.open();
        field.setFocus();
        QTRY_VERIFY(field.hasFocus());
        QTest::keyClick(owner.windowHandle(), Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(shortcutActivated.count(), 0);
        bool liveChild = false;
        bool panelBlocked = false;
        QTimer::singleShot(0, &owner, [&] {
            auto* dialog = qobject_cast<QMessageBox*>(
                choscordb::design::DialogPresentation::activeDialog(&owner));
            if (!dialog)
                return;
            liveChild = !dialog->isWindow() && dialog->window() == &owner;
            field.setFocus();
            panelBlocked = !field.hasFocus();
            auto* cancel = dialog->button(QMessageBox::Cancel);
            QTest::mouseClick(owner.windowHandle(), Qt::LeftButton, {},
                              cancel->mapTo(&owner, cancel->rect().center()));
        });
        QCOMPARE(choscordb::ConfirmationDialog::question(&panel, "Cancel task?", "Keep editing?",
                                                         QMessageBox::Ok | QMessageBox::Cancel,
                                                         QMessageBox::Cancel),
                 QMessageBox::Cancel);
        QVERIFY(liveChild);
        QVERIFY(panelBlocked);
        QVERIFY(panel.isVisible());
        QCOMPARE(choscordb::design::DialogPresentation::activeDialog(&owner), &panel);
        QTRY_VERIFY(field.hasFocus());
        const QPoint before = panel.pos();
        owner.move(owner.pos() + QPoint(30, 40));
        QCoreApplication::processEvents();
        QCOMPARE(panel.pos(), before);
        QCOMPARE(panel.geometry().center(), owner.rect().center());
        panel.reject();
        QTRY_VERIFY(original.hasFocus());
        QTest::keyClick(owner.windowHandle(), Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(shortcutActivated.count(), 1);
    }
    void parentlessModalKeepsNativeFallback() {
        choscordb::design::ModalPanel panel(nullptr);
        QVBoxLayout layout(&panel);
        layout.addWidget(new QLineEdit(&panel));
        panel.open();
        QVERIFY(panel.isWindow());
        QVERIFY(panel.isModal());
        QCOMPARE(choscordb::design::DialogPresentation::activeDialog(), &panel);
        QSignalSpy rejected(&panel, &QDialog::rejected);
        panel.reject();
        QCOMPARE(rejected.count(), 1);
    }
    void backdropRecaptureExcludesCurrentAndHigherModals() {
        QWidget owner;
        owner.resize(800, 600);
        choscordb::design::ThemeManager theme;
        theme.setMode(choscordb::design::ThemeMode::Light);
        theme.applyTo(owner);
        owner.show();
        choscordb::design::ModalPanel panel(&owner);
        QVBoxLayout layout(&panel);
        auto* marker = new QWidget(&panel);
        marker->setFixedSize(180, 80);
        marker->setStyleSheet("background: rgb(240, 20, 180)");
        layout.addWidget(marker);
        panel.open();
        auto* backdrop = owner.findChild<QWidget*>("modalBackdrop");
        QVERIFY(backdrop);
        choscordb::ConfirmationDialog nested(QMessageBox::Question, "Nested confirmation",
                                             "Close this panel?", QMessageBox::Cancel, &panel);
        nested.open();
        theme.setMode(choscordb::design::ThemeMode::Dark);
        theme.applyTo(owner);
        QCoreApplication::processEvents();
        const auto layers = owner.findChildren<QWidget*>("modalBackdrop");
        QCOMPARE(layers.size(), 2);
        const auto topImage = layers.last()->grab().toImage();
        const auto topTint = topImage.pixelColor(qRound(8 * topImage.devicePixelRatio()),
                                                 qRound(8 * topImage.devicePixelRatio()));
        QVERIFY2(topTint.lightness() < 60, qPrintable(topTint.name()));
        nested.reject();
        QVERIFY(panel.isVisible());
        const auto image = backdrop->grab().toImage();
        const auto point = marker->mapTo(&owner, marker->rect().center());
        const auto color = image.pixelColor(qRound(point.x() * image.devicePixelRatio()),
                                            qRound(point.y() * image.devicePixelRatio()));
        QVERIFY2(color.lightness() < 60, qPrintable(color.name()));
        panel.reject();
    }
    void dialogSectionsKeepCompactBarsAroundGrowingBody() {
        choscordb::design::DialogSections sections;
        sections.headerLayout()->addWidget(new QLabel("New connection", &sections));
        sections.bodyLayout()->addWidget(new QLineEdit(&sections));
        sections.footerLayout()->addWidget(new QPushButton("Save", &sections));
        sections.resize(600, 400);
        sections.show();
        QCoreApplication::processEvents();
        auto* header = sections.headerLayout()->parentWidget();
        auto* body = sections.bodyLayout()->parentWidget();
        auto* footer = sections.footerLayout()->parentWidget();
        QVERIFY(header->height() < 66);
        QVERIFY(footer->height() < 70);
        QVERIFY(body->height() > header->height());
        QCOMPARE(header->geometry().top(), 0);
        QCOMPARE(footer->geometry().bottom(), sections.rect().bottom());
        QCOMPARE(sections.bodyLayout()->contentsMargins().left(),
                 choscordb::design::spacing(choscordb::design::Spacing::Three));
        QCOMPARE(sections.bodyLayout()->contentsMargins().right(),
                 choscordb::design::spacing(choscordb::design::Spacing::Three));
        QCOMPARE(sections.headerLayout()->contentsMargins().left(),
                 choscordb::design::spacing(choscordb::design::Spacing::Three));
        QCOMPARE(sections.footerLayout()->contentsMargins().right(),
                 choscordb::design::spacing(choscordb::design::Spacing::Three));
        auto* field = body->findChild<QLineEdit*>();
        QVERIFY(field);
        QCOMPARE(field->geometry().left(),
                 choscordb::design::spacing(choscordb::design::Spacing::Three));
        for (auto* frame : sections.findChildren<QFrame*>()) {
            if (frame->frameShape() == QFrame::HLine) {
                QCOMPARE(frame->geometry().left(), 0);
                QCOMPARE(frame->geometry().right(), sections.rect().right());
            }
        }
    }
    void embeddedModalsBlockOwnerAndRestoreFocus_data() {
        QTest::addColumn<bool>("confirmation");
        QTest::newRow("panel") << false;
        QTest::newRow("confirmation") << true;
    }
    void embeddedModalsBlockOwnerAndRestoreFocus() {
        QFETCH(bool, confirmation);
        QWidget owner, other;
        QVBoxLayout ownerLayout(&owner), otherLayout(&other);
        QLineEdit original(&owner);
        QPushButton ownerAction("Owner action", &owner), otherAction("Other action", &other);
        ownerLayout.addWidget(&original);
        ownerLayout.addWidget(&ownerAction);
        otherLayout.addWidget(&otherAction);
        owner.resize(640, 480);
        other.resize(200, 100);
        other.move(800, 100);
        other.show();
        owner.show();
        owner.activateWindow();
        original.setFocus();
        QTRY_VERIFY(original.hasFocus());
        std::unique_ptr<QDialog> dialog;
        if (confirmation) {
            dialog = std::make_unique<choscordb::ConfirmationDialog>(
                QMessageBox::NoIcon, "Confirm action", "Keep this draft?", QMessageBox::Cancel,
                &owner);
        } else {
            dialog = std::make_unique<choscordb::design::ModalPanel>(&owner);
            auto* layout = new QVBoxLayout(dialog.get());
            layout->addWidget(new QLineEdit(dialog.get()));
            layout->addWidget(new QPushButton("Cancel", dialog.get()));
        }
        QSignalSpy ownerClicked(&ownerAction, &QPushButton::clicked);
        QSignalSpy otherClicked(&otherAction, &QPushButton::clicked);
        dialog->show();
        dialog->activateWindow();
        QTRY_COMPARE(choscordb::design::DialogPresentation::activeDialog(), dialog.get());
        QCOMPARE(dialog->windowModality(), Qt::NonModal);
        // Presses routed through the actual owner window hit the backdrop,
        // never an underlying control. Release is tested separately as dismissal.
        QTest::mousePress(owner.windowHandle(), Qt::LeftButton, {},
                          ownerAction.mapTo(&owner, ownerAction.rect().center()));
        QTest::mouseClick(other.windowHandle(), Qt::LeftButton, {},
                          otherAction.mapTo(&other, otherAction.rect().center()));
        QCOMPARE(ownerClicked.count(), 0);
        QCOMPARE(otherClicked.count(), 1);
        owner.activateWindow();
        dialog->setFocus();
        for (int i = 0; i < 4; ++i) {
            QTest::keyClick(dialog.get(), Qt::Key_Tab);
            auto* focused = QApplication::focusWidget();
            QVERIFY(focused && (focused == dialog.get() || dialog->isAncestorOf(focused)));
        }
        QTest::keyClick(dialog.get(), Qt::Key_Escape);
        // QMessageBox animates its Escape button on macOS before closing.
        QTRY_VERIFY(!dialog->isVisible());
        if (confirmation) {
            auto* messageBox = qobject_cast<QMessageBox*>(dialog.get());
            QVERIFY(messageBox);
            QCOMPARE(messageBox->standardButton(messageBox->clickedButton()), QMessageBox::Cancel);
        }
        QTRY_VERIFY(original.hasFocus());
        // A positive control verifies the same window-level input reaches the
        // underlying action once the application modal session has ended.
        QTest::mouseClick(other.windowHandle(), Qt::LeftButton, {},
                          otherAction.mapTo(&other, otherAction.rect().center()));
        QCOMPARE(otherClicked.count(), 2);
    }
    void backdropDoesNotPaintBlackShadowSourceBeneathThePanel() {
        QWidget parent;
        parent.resize(960, 640);
        choscordb::design::ThemeManager theme;
        theme.setMode(choscordb::design::ThemeMode::Light);
        theme.applyTo(parent);
        parent.show();
        choscordb::design::ModalPanel panel(&parent);
        QVBoxLayout layout(&panel);
        layout.addWidget(new QLineEdit(&panel));
        panel.show();
        auto* backdrop = parent.findChild<QWidget*>("modalBackdrop");
        QVERIFY(backdrop && backdrop->isVisible());
        // The sibling backdrop must contain only the dimmed canvas and the
        // exterior shadow, never an opaque shadow source or mirrored panel.
        const auto capture = backdrop->grab();
        const auto image = capture.toImage();
        const auto center = backdrop->mapFromGlobal(panel.mapToGlobal(panel.rect().center()));
        const auto color = image.pixelColor(qRound(center.x() * capture.devicePixelRatio()),
                                            qRound(center.y() * capture.devicePixelRatio()));
        QVERIFY2(color.lightness() > 150, qPrintable(color.name()));
        QTest::keyClick(&panel, Qt::Key_Escape);
        QVERIFY(!panel.isVisible());
    }
    void ownerWindowCaptureIncludesModalContent() {
        class CaptureMarker final : public QWidget {
          protected:
            void paintEvent(QPaintEvent*) override {
                QPainter painter(this);
                painter.fillRect(rect(), QColor(240, 20, 180));
            }
        };
        QWidget parent;
        parent.resize(960, 640);
        parent.setStyleSheet("background: rgb(20, 40, 60);");
        parent.show();
        choscordb::design::ModalPanel panel(&parent);
        QVBoxLayout layout(&panel);
        auto* marker = new CaptureMarker;
        marker->setFixedSize(180, 80);
        layout.addWidget(marker);
        panel.show();
        QCoreApplication::processEvents();

        // The live child surface must be in the owner's own backing store.
        const auto capture = parent.grab();
        const auto point = parent.mapFromGlobal(marker->mapToGlobal(marker->rect().center()));
        const auto color =
            capture.toImage().pixelColor(qRound(point.x() * capture.devicePixelRatio()),
                                         qRound(point.y() * capture.devicePixelRatio()));
        QVERIFY2(color.red() > 220 && color.green() < 40 && color.blue() > 160,
                 qPrintable(color.name()));
        panel.reject();
    }
    void nativeCompositorKeepsAppOwnedModalCorners_data() {
        QTest::addColumn<bool>("confirmation");
        QTest::newRow("panel") << false;
        QTest::newRow("confirmation") << true;
    }
    void nativeCompositorKeepsAppOwnedModalCorners() {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Composited native corners require the Cocoa platform.");
        QFETCH(bool, confirmation);
        QWidget parent;
        parent.setGeometry(100, 100, 960, 640);
        choscordb::design::ThemeManager theme;
        theme.setMode(choscordb::design::ThemeMode::Light);
        theme.applyTo(parent);
        parent.show();
        parent.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&parent));
        std::unique_ptr<QDialog> dialog;
        if (confirmation) {
            dialog = std::make_unique<choscordb::ConfirmationDialog>(
                QMessageBox::NoIcon, "Confirm action", "Keep this draft?", QMessageBox::Cancel,
                &parent);
        } else {
            dialog = std::make_unique<choscordb::design::ModalPanel>(&parent);
            auto* layout = new QVBoxLayout(dialog.get());
            layout->addWidget(new QLineEdit(dialog.get()));
        }
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(dialog.get()));
        QTest::qWait(150); // Allow the OS compositor to present the shown window.
        const auto topLeft = dialog->mapToGlobal(QPoint());
        const auto capture = dialog->screen()->grabWindow(0, topLeft.x(), topLeft.y(),
                                                          dialog->width(), dialog->height());
        if (capture.isNull())
            QSKIP("Screen capture permission unavailable; native corner review remains required.");
        const auto captureDirectory = qEnvironmentVariable("CHOSCORDB_TEST_CAPTURE_DIR");
        if (!captureDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(captureDirectory));
            QVERIFY(capture.save(QDir(captureDirectory)
                                     .filePath(confirmation ? "native-confirmation-composited.png"
                                                            : "native-modal-composited.png")));
        }
        const auto color = capture.toImage().pixelColor(qRound(5 * capture.devicePixelRatio()),
                                                        qRound(5 * capture.devicePixelRatio()));
        // (5,5) is inside the painted 8px corner, but outside macOS's larger
        // dialog clipping radius. Capture the compositor, not QWidget::grab.
        QVERIFY2(color.red() > 240 && color.green() > 240 && color.blue() > 240,
                 qPrintable(color.name()));
        QVERIFY(!dialog->isWindow());
        QTest::keyClick(dialog.get(), Qt::Key_Escape);
        // QMessageBox animates its Escape button on macOS before closing.
        QTRY_VERIFY(!dialog->isVisible());
        if (confirmation) {
            auto* messageBox = qobject_cast<QMessageBox*>(dialog.get());
            QVERIFY(messageBox);
            QCOMPARE(messageBox->standardButton(messageBox->clickedButton()), QMessageBox::Cancel);
        }
    }
    void modalSoftensBackgroundDetailAndRestoresItOnClose() {
        class StripedOwner final : public QWidget {
          protected:
            void paintEvent(QPaintEvent*) override {
                QPainter painter(this);
                for (int x = 0; x < width(); x += 2)
                    painter.fillRect(x, 0, 2, height(), x % 4 == 0 ? Qt::black : Qt::white);
            }
        } parent;
        parent.resize(960, 640);
        parent.show();
        const auto before = parent.grab().toImage();
        choscordb::design::ModalPanel panel(&parent);
        QVBoxLayout layout(&panel);
        layout.addWidget(new QLineEdit(&panel));
        panel.show();
        const auto after = parent.grab().toImage();
        const auto scale = after.devicePixelRatio();
        const int contrast =
            qAbs(after.pixelColor(qRound(20 * scale), qRound(8 * scale)).lightness() -
                 after.pixelColor(qRound(22 * scale), qRound(8 * scale)).lightness());
        QVERIFY2(contrast < 40,
                 "The reference backdrop blurs fine background detail, not just dims it.");
        panel.reject();
        QCOMPARE(parent.grab().toImage(), before);
    }

    void openPanelCastsAVisibleShadowOutsideItsBounds() {
        QWidget parent;
        parent.resize(960, 640);
        choscordb::design::ThemeManager theme;
        theme.setMode(choscordb::design::ThemeMode::Light);
        theme.applyTo(parent);
        parent.show();
        choscordb::design::ModalPanel panel(&parent);
        QVBoxLayout layout(&panel);
        layout.addWidget(new QLineEdit(&panel));
        panel.show();
        auto* backdrop = parent.findChild<QWidget*>("modalBackdrop");
        QVERIFY(backdrop && backdrop->isVisible());
        const auto image = parent.grab().toImage();
        const auto scale = image.devicePixelRatio();
        const auto below =
            panel.mapTo(&parent, panel.rect().bottomLeft()) + QPoint(panel.width() / 2, 8);
        const QPoint sample(qRound(below.x() * scale), qRound(below.y() * scale));
        QVERIFY(image.rect().contains(sample));
        QVERIFY2(image.pixelColor(sample).lightness() <
                     image.pixelColor(qRound(8 * scale), qRound(8 * scale)).lightness(),
                 "A modal must cast its reference shadow beyond the panel, over the dimmed owner.");
        panel.reject();
    }

    void modalBackdropUsesReferenceTintAndRestoresItsOwner() {
        QWidget parent;
        parent.resize(960, 640);
        choscordb::design::ThemeManager theme;
        theme.setMode(choscordb::design::ThemeMode::Light);
        theme.applyTo(parent);
        parent.show();
        const auto before = parent.grab().toImage();
        choscordb::design::ModalPanel panel(&parent);
        QVBoxLayout layout(&panel);
        layout.addWidget(new QLineEdit(&panel));
        panel.show();
        auto* backdrop = parent.findChild<QWidget*>("modalBackdrop");
        QVERIFY(backdrop && backdrop->isVisible());
        const auto rendered = parent.grab().toImage();
        const auto scale = rendered.devicePixelRatio();
        // CSS #192c3650 composited over the reference #f6f7f8 canvas.
        const auto tint = rendered.pixelColor(qRound(8 * scale), qRound(8 * scale));
        QCOMPARE(tint.alpha(), 255);
        QVERIFY(qAbs(tint.red() - 177) <= 1);
        QVERIFY(qAbs(tint.green() - 183) <= 1);
        QVERIFY(qAbs(tint.blue() - 187) <= 1);
        theme.setMode(choscordb::design::ThemeMode::Dark);
        theme.applyTo(parent);
        QCoreApplication::processEvents();
        const auto dark = parent.grab().toImage();
        const auto darkTint = dark.pixelColor(qRound(8 * scale), qRound(8 * scale));
        QCOMPARE(parent.palette().color(QPalette::Window), QColor("#171d20"));
        // The same tint over the new #171d20 canvas must replace the light snapshot.
        // Qt's 8-bit blur/compositing rounds the dark channels by up to two.
        // This still rejects the stale light snapshot by more than 150 levels.
        QVERIFY2(qAbs(darkTint.red() - 24) <= 2, qPrintable(darkTint.name()));
        QVERIFY(qAbs(darkTint.green() - 34) <= 2);
        QVERIFY(qAbs(darkTint.blue() - 39) <= 2);
        theme.setMode(choscordb::design::ThemeMode::Light);
        theme.applyTo(parent);
        QCoreApplication::processEvents();
        const auto restored = parent.grab().toImage();
        QCOMPARE(restored.pixelColor(qRound(8 * scale), qRound(8 * scale)), tint);
        QTest::keyClick(&panel, Qt::Key_Escape);
        QVERIFY(!backdrop->isVisible());
        QCOMPARE(parent.grab().toImage(), before);
    }

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
                 QColor("#f9eeee"));
        dialog.reject();
    }
    void shortConfirmationUsesCompactLayout() {
        QWidget parent;
        parent.resize(960, 640);
        parent.show();
        choscordb::ConfirmationDialog dialog(
            QMessageBox::Warning, "Delete synthetic record?",
            "This preview records your choice only. No data is changed.", QMessageBox::Cancel,
            &parent);
        dialog.addButton("Delete", QMessageBox::DestructiveRole);
        dialog.show();
        QApplication::processEvents();
        auto* heading = dialog.findChild<QLabel*>("confirmationHeading");
        auto* body = dialog.findChild<QScrollArea*>("confirmationBodyScroll");
        auto* footer = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(heading && body && footer);
        auto* icon = dialog.findChild<QLabel*>("qt_msgboxex_icon_label");
        auto* cancel = dialog.button(QMessageBox::Cancel);
        auto* destructive = dialog.buttons().last();
        QVERIFY(icon && cancel && destructive);
        QVERIFY(body->x() - icon->geometry().right() <= 12);
        QVERIFY(cancel->x() - destructive->geometry().right() <= 12);
        QCOMPARE(dialog.width(), 440);
        QCOMPARE(body->verticalScrollBar()->maximum(), 0);
        QVERIFY(body->height() <= 44);
        QVERIFY(body->y() - heading->geometry().bottom() <= 20);
        QVERIFY(footer->y() - body->geometry().bottom() <= 20);
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
        QVERIFY(ownerRect.contains(QRect(dialog.mapToGlobal(QPoint()), dialog.size())));
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
        QCOMPARE(dialog.width(), 440);
        parent.resize(800, 600);
        QApplication::processEvents();
        QCOMPARE(dialog.geometry().center(), parent.rect().center());
        auto* backdrop = parent.findChild<QWidget*>("modalBackdrop");
        QVERIFY(backdrop);
        QCOMPARE(backdrop->geometry(), parent.rect());
        dialog.reject();
    }
    void openConfirmationRecentersAfterWindowZoomSettles() {
        QWidget parent;
        parent.setGeometry(100, 100, 640, 480);
        parent.show();
        choscordb::ConfirmationDialog dialog(QMessageBox::Warning, "Connection failed",
                                             "Could not open Example Postgres.", QMessageBox::Ok,
                                             &parent);
        dialog.show();
        const auto stalePosition = dialog.pos();

        // A queued stale layout position must be superseded by the final
        // owner geometry after a zoom/resize transition.
        QTimer::singleShot(0, &dialog, [&dialog, stalePosition] { dialog.move(stalePosition); });
        parent.resize(960, 640);
        QCoreApplication::processEvents();

        QCOMPARE(dialog.geometry().center(), parent.rect().center());
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
            auto* dialog =
                qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
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
                 QColor("#20272b"));
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
        QCOMPARE(dialog.geometry().center(), parent.rect().center());
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
        QCOMPARE(panel.width(), 608);
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
        QCOMPARE(image.pixelColor(100, 60), QColor("#20272b"));
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
        QVERIFY(!panel.isWindow());
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
