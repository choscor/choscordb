#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/control_style.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/field/field.h"
#include "design_system/history_row/history_row.h"
#include "design_system/menu/menu.h"
#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/quick_search/quick_search_dialog.h"
#include "design_system/table/table_style.h"
#include "design_system/tabs/tab_add_corner.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
#include "design_system/toast_region/toast_region.h"
#include "models/navigator_model.h"
#include "models/result_table_model.h"
#include "preview_test.h"
#include "preview_test_helpers.h"
#include "tools/preview/preview_window.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFocusEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QSvgRenderer>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QtTest>
#include <cstring>

void PreviewTest::quickSearchSpecimenUsesRealOverlayInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("quick-search"));
    window.show();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* open = host->findChild<QPushButton*>("previewOpenQuickSearch");
        auto* dialog =
            previewSurface<choscordb::design::QuickSearchDialog>(host, "previewOpenQuickSearch");
        QVERIFY(open && dialog);
        open->click();
        QTRY_VERIFY(dialog->isVisible());
        auto* input = dialog->findChild<QLineEdit*>("quickSearchInput");
        auto* list = dialog->findChild<QListWidget*>("quickSearchResults");
        auto* status = dialog->findChild<QLabel*>("quickSearchStatus");
        QVERIFY(input && list && status);
        input->setText(QString(200, QChar('x')));
        QCOMPARE(input->text().size(), 128);
        input->clear();
        QCOMPARE(list->count(), 3);
        QVERIFY(!input->accessibleName().isEmpty());
        QVERIFY(!list->accessibleName().isEmpty());
        QVERIFY(!status->accessibleName().isEmpty());
        QCOMPARE(dialog->window(), &window);
        QVERIFY(dialog->y() < window.height() / 3);
        dialog->reject();
    }
}

void PreviewTest::quickSearchInteractionPreservesFocusAndSelection() {
    QWidget owner;
    owner.resize(900, 700);
    QLineEdit previous(&owner);
    previous.setGeometry(20, 20, 200, 32);
    choscordb::design::QuickSearchDialog dialog(&owner);
    owner.show();
    owner.activateWindow();
    previous.setFocus();
    QTRY_VERIFY(previous.hasFocus());
    QSignalSpy queries(&dialog, &choscordb::design::QuickSearchDialog::queryChanged);
    QSignalSpy activated(&dialog, &choscordb::design::QuickSearchDialog::activated);
    dialog.openSearch();
    QTRY_VERIFY(dialog.findChild<QLineEdit*>("quickSearchInput")->hasFocus());
    QCOMPARE(queries.count(), 1);
    dialog.setResults({{"Screen", "Query", "Workspace", "screen:query"},
                       {"Object", "customers", "public · table", "object:customers"}});
    auto* input = dialog.findChild<QLineEdit*>("quickSearchInput");
    auto* list = dialog.findChild<QListWidget*>("quickSearchResults");
    auto* status = dialog.findChild<QLabel*>("quickSearchStatus");
    QVERIFY(status);
    dialog.setLoading(true);
    QCOMPARE(status->property("state").toString(), QString("loading"));
    QVERIFY(status->accessibleDescription().contains("Searching"));
    dialog.setError("History unavailable");
    QCOMPARE(status->property("state").toString(), QString("error"));
    QVERIFY(status->accessibleDescription().contains("History unavailable"));
    dialog.setLoading(false);
    dialog.setError({});
    QCOMPARE(dialog.selectedResultId(), QString("screen:query"));
    QTest::keyClick(input, Qt::Key_Down);
    QCOMPARE(dialog.selectedResultId(), QString("object:customers"));
    QVERIFY(list->accessibleDescription().contains("customers"));
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.at(0).at(0).toString(), QString("object:customers"));
    QVERIFY(dialog.isVisible());
    dialog.openSearch();
    QCOMPARE(queries.count(), 1);
    QVERIFY(input->hasFocus());
    input->setText("customers");
    QCOMPARE(dialog.query(), QString("customers"));
    QCOMPARE(queries.count(), 2);
    QTest::keyClick(input, Qt::Key_Escape);
    QTRY_VERIFY(!dialog.isVisible());
    QTRY_VERIFY(previous.hasFocus());
    dialog.openSearch();
    QCOMPARE(queries.count(), 3);
    QVERIFY(dialog.query().isEmpty());
    dialog.setResults({{"Screen", "Query", "Workspace", "screen:query"}});
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(0)).center());
    QCOMPARE(activated.count(), 2);
    QCOMPARE(activated.at(1).at(0).toString(), QString("screen:query"));
    dialog.reject();
}

void PreviewTest::quickSearchKeepsSelectionAcrossResultUpdates() {
    QWidget owner;
    owner.resize(900, 700);
    choscordb::design::QuickSearchDialog dialog(&owner);
    owner.show();
    dialog.openSearch();
    auto* input = dialog.findChild<QLineEdit*>("quickSearchInput");
    auto* list = dialog.findChild<QListWidget*>("quickSearchResults");
    QVERIFY(input && list);
    dialog.setResults({{"Screen", "Query", "Workspace", "screen:query"},
                       {"Object", "customers", "public · table", "object:customers"}});
    QTest::keyClick(input, Qt::Key_Down);
    QCOMPARE(dialog.selectedResultId(), QString("object:customers"));

    dialog.setResults({{"Screen", "Query", "Workspace", "screen:query"},
                       {"History", "saved query", "Today", "history:1"},
                       {"Object", "customers", "public · table", "object:customers"}});
    QCOMPARE(dialog.selectedResultId(), QString("object:customers"));
    QCOMPARE(list->currentRow(), 2);
    QVERIFY(list->accessibleDescription().contains("customers"));

    dialog.setResults({{"History", "saved query", "Today", "history:1"},
                       {"Screen", "Query", "Workspace", "screen:query"}});
    QCOMPARE(dialog.selectedResultId(), QString("history:1"));
    QCOMPARE(list->currentRow(), 0);
    dialog.reject();
}
