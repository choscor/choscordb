#include "app/object_explorer.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/theme_manager.h"
#include "design_system/toast_region/toast_region.h"
#include "screen_state_checks.h"
#include "widgets/object_erd_widget.h"
#include <QAction>
#include <QFrame>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabBar>
#include <QTableView>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>
#include <QWheelEvent>
using namespace choscordb;
class ObjectExplorerDdlTest final : public QObject {
    Q_OBJECT
  private slots:
    void ddlTextHasTopAndLeftBreathingRoom() {
        EngineAdapter adapter;
        ObjectExplorer explorer(&adapter);
        auto* ddl = explorer.findChild<QPlainTextEdit*>("objectDdl");
        QVERIFY(ddl);
        explorer.findChild<QStackedWidget*>()->setCurrentWidget(ddl);
        explorer.resize(700, 450);
        explorer.show();
        ddl->setPlainText("CREATE VIEW sample AS SELECT 1;");
        QCoreApplication::processEvents();
        ddl->moveCursor(QTextCursor::Start);
        const QRect firstCharacter = ddl->cursorRect();
        QVERIFY2(firstCharacter.top() >= 8, "DDL text needs at least 8 px top inset");
        QVERIFY2(firstCharacter.left() >= 8, "DDL text needs at least 8 px left inset");
    }
    void ddlMatchesSqlEditorTypographyAndSyntaxPalette() {
        EngineAdapter adapter;
        ObjectExplorer explorer(&adapter);
        design::ThemeManager theme;
        theme.setMode(design::ThemeMode::Light);
        theme.applyTo(explorer);
        auto* ddl = explorer.findChild<QPlainTextEdit*>("objectDdl");
        QVERIFY(ddl);
        QCOMPARE(ddl->font().family(),
                 design::resolveTypography(design::TextStyle::Body, design::FontDesign::Monospaced)
                     .family());
        QCOMPARE(ddl->font().pixelSize(), 14);
        QCOMPARE(ddl->frameShape(), QFrame::NoFrame);
        ddl->setPlainText("SELECT 'x', 42; -- note");
        QCoreApplication::processEvents();
        const auto formats = ddl->document()->firstBlock().layout()->formats();
        const auto colorAt = [&](int offset) {
            for (const auto& range : formats)
                if (offset >= range.start && offset < range.start + range.length)
                    return range.format.foreground().color();
            return QColor{};
        };
        QCOMPARE(colorAt(0), QColor("#d73a49"));
        QCOMPARE(colorAt(7), QColor("#032f62"));
        QCOMPARE(colorAt(12), QColor("#005cc5"));
        QCOMPARE(colorAt(16), QColor("#6a737d"));
    }
    void ddlRecolorsWithThemeAndKeepsCommentNumbersMuted() {
        EngineAdapter adapter;
        ObjectExplorer explorer(&adapter);
        design::ThemeManager theme;
        theme.setMode(design::ThemeMode::Light);
        theme.applyTo(explorer);
        auto* ddl = explorer.findChild<QPlainTextEdit*>("objectDdl");
        QVERIFY(ddl);
        ddl->setPlainText("SELECT 42; -- 99");
        const auto colorAt = [&](int offset) {
            QCoreApplication::processEvents();
            for (const auto& range : ddl->document()->firstBlock().layout()->formats())
                if (offset >= range.start && offset < range.start + range.length)
                    return range.format.foreground().color();
            return QColor{};
        };
        QCOMPARE(colorAt(14), QColor("#6a737d"));
        theme.setMode(design::ThemeMode::Dark);
        theme.applyTo(explorer);
        QCOMPARE(colorAt(0), QColor("#a0a0a0"));
        QCOMPARE(colorAt(14), QColor(0x8b, 0x8b, 0x8b, 0x94));
    }
    void ddlUsesSqlSyntaxColors() {
        EngineAdapter adapter;
        ObjectExplorer explorer(&adapter);
        auto* ddl = explorer.findChild<QPlainTextEdit*>("objectDdl");
        QVERIFY(ddl);
        ddl->setPlainText("CREATE TABLE customers (name TEXT DEFAULT 'Alice'); -- note");
        QCoreApplication::processEvents();
        const auto formats = ddl->document()->firstBlock().layout()->formats();
        const auto colorAt = [&](int offset) {
            for (const auto& range : formats)
                if (offset >= range.start && offset < range.start + range.length)
                    return range.format.foreground().color();
            return QColor{};
        };
        QVERIFY(colorAt(0).isValid());
        QVERIFY(colorAt(36).isValid());
        QVERIFY(colorAt(47).isValid());
        QVERIFY(colorAt(0) != colorAt(47));
        ddl->setPlainText("CREATE TABLE \"SELECT\" (name TEXT DEFAULT '-- active'); -- note");
        QCoreApplication::processEvents();
        const auto quotedFormats = ddl->document()->firstBlock().layout()->formats();
        const auto quotedColor = [&](int offset) {
            for (const auto& range : quotedFormats)
                if (offset >= range.start && offset < range.start + range.length)
                    return range.format.foreground().color();
            return QColor{};
        };
        QVERIFY(quotedColor(14).isValid());
        QVERIFY(quotedColor(14) != quotedColor(0));
        QVERIFY(quotedColor(43) != quotedColor(55));
        ddl->setPlainText("CREATE /* SELECT\nFROM */ TABLE items DEFAULT 'hello\nSELECT';");
        QCoreApplication::processEvents();
        const auto second = ddl->document()->findBlockByNumber(1).layout()->formats();
        const auto third = ddl->document()->findBlockByNumber(2).layout()->formats();
        const auto formatAt = [](const auto& ranges, int offset) {
            for (const auto& range : ranges)
                if (offset >= range.start && offset < range.start + range.length)
                    return range.format.foreground().color();
            return QColor{};
        };
        QVERIFY(formatAt(second, 0).isValid());
        QVERIFY(formatAt(second, 0) != formatAt(second, 8));
        QVERIFY(formatAt(third, 0).isValid());
        QVERIFY(formatAt(third, 0) != formatAt(second, 8));
        ddl->setPlainText("'SELECT' CREATE");
        QCoreApplication::processEvents();
        const auto firstQuoted = ddl->document()->firstBlock().layout()->formats();
        QVERIFY(formatAt(firstQuoted, 1).isValid());
        QCOMPARE(formatAt(firstQuoted, 1), formatAt(firstQuoted, 0));
        QVERIFY(formatAt(firstQuoted, 1) != formatAt(firstQuoted, 9));
    }
};
QTEST_MAIN(ObjectExplorerDdlTest)
#include "object_explorer_ddl_test.moc"
