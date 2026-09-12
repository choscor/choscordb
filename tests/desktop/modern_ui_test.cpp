#include "app/appearance_controller.h"
#include "app/main_window.h"
#include "app/query_workspace.h"
#include "design_system/theme_manager.h"
#include "widgets/sql_editor.h"
#include "widgets/toast_region.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyle>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <Qsci/qscilexersql.h>
#include <Qsci/qsciscintilla.h>

class ModernUiTest final : public QObject {
    Q_OBJECT

  private slots:
    void workspaceProvidesDiscoverableModernControls() {
        choscordb::MainWindow window;

        auto* navigatorTitle = window.findChild<QLabel*>("navigatorTitle");
        auto* addConnection = window.findChild<QPushButton*>("navigatorAddConnection");
        auto* summary = window.findChild<QLabel*>("executionSummary");
        auto* toast = window.findChild<choscordb::ToastRegion*>("toastRegion");
        auto* resetLayout = window.findChild<QAction*>("resetLayout");

        QVERIFY(navigatorTitle);
        QCOMPARE(navigatorTitle->text(), QString("Connections"));
        QVERIFY(addConnection);
        QVERIFY(!addConnection->accessibleName().isEmpty());
        QVERIFY(!addConnection->toolTip().isEmpty());
        QVERIFY(summary);
        QCOMPARE(summary->property("state").toString(), QString("disconnected"));
        QVERIFY(!summary->accessibleName().isEmpty());
        QVERIFY(toast);
        QVERIFY(!toast->accessibleName().isEmpty());
        QVERIFY(toast->isHidden());
        toast->showNotice("First notice", 10000);
        toast->showNotice("Replacement notice", 10000);
        QCOMPARE(toast->text(), QString("Replacement notice"));
        QVERIFY(resetLayout);
        auto* run = window.findChild<QAction*>("runStatement");
        QVERIFY(run);
        QVERIFY(!run->icon().isNull());
        QVERIFY(QFile::exists(":/icons/app-mark.svg"));
    }

    void navigatorContextActionsSupportKeyboardFocusAndMenus() {
        choscordb::MainWindow window;
        auto* tree = window.findChild<QTreeView*>();
        auto* refresh = window.findChild<QPushButton*>("navigatorRefresh");
        auto* disconnect = window.findChild<QPushButton*>("navigatorDisconnect");
        auto* refreshAction = window.findChild<QAction*>("navigatorRefreshAction");
        auto* disconnectAction = window.findChild<QAction*>("navigatorDisconnectAction");
        QVERIFY(tree);
        QVERIFY(refresh);
        QVERIFY(disconnect);
        QVERIFY(refreshAction);
        QVERIFY(disconnectAction);
        QCOMPARE(tree->contextMenuPolicy(), Qt::CustomContextMenu);
        QVERIFY(refresh->isHidden());
        QVERIFY(disconnect->isHidden());
        QVERIFY(tree->actions().contains(refreshAction));
        QVERIFY(tree->actions().contains(disconnectAction));

        window.show();
        tree->setFocus();
        QTRY_VERIFY(refresh->isVisible());
        QTRY_VERIFY(disconnect->isVisible());
    }

    void narrowWorkspaceMovesSecondaryQueryActionsIntoOverflow() {
        choscordb::MainWindow window;
        auto* overflow = window.findChild<QToolButton*>("queryToolbarOverflow");
        QVERIFY(overflow);
        QCOMPARE(overflow->menu()->actions().size(), 2);
        QVERIFY(!overflow->accessibleName().isEmpty());

        window.resize(960, 640);
        window.show();
        QTRY_VERIFY(overflow->isVisible());
        for (auto* action : overflow->menu()->actions()) {
            auto* toolbarWidget = window.findChild<QToolBar*>()->widgetForAction(action);
            QVERIFY(toolbarWidget);
            QVERIFY(!toolbarWidget->isVisible());
        }

        window.resize(1280, 900);
        QTRY_VERIFY(!overflow->isVisible());
        for (auto* action : overflow->menu()->actions()) {
            QVERIFY(window.findChild<QToolBar*>()->widgetForAction(action)->isVisible());
        }
    }

    void inactiveEditorCloseButtonAppearsOnHover() {
        choscordb::MainWindow window;
        auto* editors = window.findChild<QTabWidget*>("editorTabs");
        auto* newQuery = window.findChild<QAction*>("newQuery");
        QVERIFY(editors);
        QVERIFY(newQuery);
        newQuery->trigger();
        newQuery->trigger();
        QCOMPARE(editors->count(), 3);
        window.show();
        QCoreApplication::processEvents();

        auto* tabs = editors->tabBar();
        const auto side = static_cast<QTabBar::ButtonPosition>(
            tabs->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, tabs));
        const int inactive = editors->currentIndex() == 0 ? 1 : 0;
        auto* close = tabs->tabButton(inactive, side);
        QVERIFY(close);
        QVERIFY(!close->isVisible());
        QTest::mouseMove(tabs, tabs->tabRect(inactive).center());
        QTRY_VERIFY(close->isVisible());
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(tabs, &leave);
        QTRY_VERIFY(!close->isVisible());
    }

    void paletteUpdatePreservesCompleteEditorState() {
        const auto originalPalette = qApp->palette();
        const auto originalStyleSheet = qApp->styleSheet();
        choscordb::design::ThemeManager theme;
        theme.installOn(qApp);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        choscordb::SqlEditor editor;
        editor.setText("SELECT alpha FROM sample;\nSELECT beta;");
        const auto path = directory.filePath("theme-state.sql");
        QSignalSpy saved(&editor, &choscordb::SqlEditor::fileSaved);
        editor.saveFile(path);
        QTRY_COMPARE(saved.count(), 1);
        QCOMPARE(saved.at(0).at(0).toString(), path);
        QVERIFY(saved.at(0).at(1).toString().isEmpty());
        editor.setModified(true);
        editor.setProfileId("profile-a");
        editor.SendScintilla(QsciScintilla::SCI_SETSEL, 7UL, 12L);
        editor.SendScintilla(QsciScintilla::SCI_SETFIRSTVISIBLELINE, 1);
        editor.insert("x");
        QVERIFY(editor.isUndoAvailable());
        const auto* lexer = editor.lexer();
        const auto keywordMeaning = lexer->description(QsciLexerSQL::Keyword);
        const auto bytes = editor.text().toUtf8();
        const auto cursor = editor.SendScintilla(QsciScintilla::SCI_GETCURRENTPOS);
        const auto anchor = editor.SendScintilla(QsciScintilla::SCI_GETANCHOR);
        const auto firstLine = editor.SendScintilla(QsciScintilla::SCI_GETFIRSTVISIBLELINE);

        theme.setMode(choscordb::design::ThemeMode::Dark);
        QCoreApplication::processEvents();

        QCOMPARE(editor.text().toUtf8(), bytes);
        QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_GETCURRENTPOS), cursor);
        QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_GETANCHOR), anchor);
        QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_GETFIRSTVISIBLELINE), firstLine);
        QVERIFY(editor.isModified());
        QVERIFY(editor.isUndoAvailable());
        QCOMPARE(editor.filePath(), path);
        QCOMPARE(editor.lexer(), lexer);
        QCOMPARE(editor.lexer()->description(QsciLexerSQL::Keyword), keywordMeaning);
        editor.undo();
        QVERIFY(editor.isRedoAvailable());
        editor.redo();
        QCOMPARE(editor.text().toUtf8(), bytes);
        QCOMPARE(editor.property("profileId").toString(), QString("profile-a"));
        theme.installOn(nullptr);
        qApp->setPalette(originalPalette);
        qApp->setStyleSheet(originalStyleSheet);
    }

    void appearancePreviewsPersistAndRestoreAcrossRestart() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("ui.sqlite");
        {
            choscordb::MainWindow window(nullptr, path);
            auto* appearance = window.findChild<choscordb::AppearanceController*>();
            auto* theme = window.findChild<choscordb::design::ThemeManager*>();
            QVERIFY(appearance);
            QVERIFY(theme);
            QTRY_VERIFY(appearance->isReady());
            QSignalSpy warning(appearance, &choscordb::AppearanceController::warningChanged);
            QVERIFY(appearance->preview("dark", "comfortable", "custom", "#2468B2"));
            QCOMPARE(theme->mode(), choscordb::design::ThemeMode::Dark);
            QCOMPARE(theme->density(), choscordb::design::Density::Comfortable);
            const auto acceptedAccent = theme->accent();
            QVERIFY(!appearance->preview("dark", "comfortable", "custom", "#FFFFFF"));
            QCOMPARE(theme->accent(), acceptedAccent);
            QVERIFY(!warning.isEmpty());
            QVERIFY(!warning.last().at(0).toString().isEmpty());
            QSignalSpy saved(appearance, &choscordb::AppearanceController::saveFinished);
            appearance->applyPreview();
            QTRY_COMPARE(saved.count(), 1);
            QVERIFY2(saved.at(0).at(0).toBool(), qPrintable(saved.at(0).at(1).toString()));
            appearance->resetLayout();
            QCOMPARE(theme->mode(), choscordb::design::ThemeMode::Dark);
            QTest::qWait(400);
        }
        {
            choscordb::MainWindow window(nullptr, path);
            auto* appearance = window.findChild<choscordb::AppearanceController*>();
            auto* theme = window.findChild<choscordb::design::ThemeManager*>();
            QVERIFY(appearance);
            QVERIFY(theme);
            QTRY_VERIFY(appearance->isReady());
            QCOMPARE(theme->mode(), choscordb::design::ThemeMode::Dark);
            QCOMPARE(theme->density(), choscordb::design::Density::Comfortable);
            QCOMPARE(theme->accent().customColor.name(), QString("#2468b2"));
            appearance->reset();
            QTRY_COMPARE(theme->mode(), choscordb::design::ThemeMode::System);
            QCOMPARE(theme->density(), choscordb::design::Density::Compact);
        }
    }

    void preferencesUseSectionNavigationAndCancelableLivePreview() {
        choscordb::MainWindow window;
        auto* appearance = window.findChild<choscordb::AppearanceController*>();
        auto* theme = window.findChild<choscordb::design::ThemeManager*>();
        QTRY_VERIFY(appearance->isReady());
        window.findChild<QAction*>("preferences")->trigger();
        auto* dialog = window.findChild<QDialog*>("preferencesDialog");
        QVERIFY(dialog);
        auto* sections = dialog->findChild<QListWidget*>("preferencesSections");
        auto* mode = dialog->findChild<QComboBox*>("appearanceTheme");
        auto* density = dialog->findChild<QComboBox*>("appearanceDensity");
        auto* accent = dialog->findChild<QComboBox*>("appearanceAccent");
        auto* custom = dialog->findChild<QLineEdit*>("appearanceCustomAccent");
        auto* status = dialog->findChild<QLabel*>("appearanceStatus");
        auto* apply = dialog->findChild<QPushButton*>("preferencesApply");
        QVERIFY(sections);
        QCOMPARE(sections->count(), 3);
        QCOMPARE(dialog->layout()->contentsMargins().left(), 16);
        QTRY_VERIFY(apply->isEnabled());
        mode->setCurrentIndex(mode->findData("dark"));
        density->setCurrentIndex(density->findData("comfortable"));
        QCOMPARE(theme->mode(), choscordb::design::ThemeMode::Dark);
        QCOMPARE(theme->density(), choscordb::design::Density::Comfortable);
        QCOMPARE(dialog->layout()->contentsMargins().left(), 20);
        accent->setCurrentIndex(accent->findData("custom"));
        custom->setText("#FFFFFF");
        QVERIFY(!status->text().isEmpty());
        QVERIFY(!apply->isEnabled());
        custom->setText("#2468B2");
        QVERIFY(apply->isEnabled());
        theme->setForcedContrast(true);
        QVERIFY(status->text().contains("high-contrast", Qt::CaseInsensitive));
        theme->setForcedContrast(false);
        dialog->reject();
        QCOMPARE(theme->mode(), choscordb::design::ThemeMode::System);
        QCOMPARE(theme->density(), choscordb::design::Density::Compact);
    }

    void executionStripKeepsVisibleAndAccessibleTerminalState() {
        choscordb::MainWindow window;
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        auto* run = window.findChild<QAction*>("runStatement");
        auto* summary = window.findChild<QLabel*>("executionSummary");
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        workspace->connectSqlite(":memory:");
        QTRY_VERIFY(run->isEnabled());
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        editor->setText("SELECT missing FROM nowhere");
        run->trigger();
        QTRY_COMPARE(summary->property("state").toString(), QString("failed"));
        QTRY_VERIFY(summary->accessibleName().contains("failed", Qt::CaseInsensitive));
        editor->setText("SELECT 1 AS value");
        run->trigger();
        QTRY_COMPARE(summary->property("state").toString(), QString("completed"));
        QTRY_VERIFY(summary->text().contains("ms"));
        QVERIFY(summary->accessibleName().contains("Completed"));
    }
};

QTEST_MAIN(ModernUiTest)
#include "modern_ui_test.moc"
