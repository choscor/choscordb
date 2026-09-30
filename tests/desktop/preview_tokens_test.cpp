#include "preview_test.h"
#include "tools/preview/preview_window.h"

#include <QApplication>
#include <QClipboard>
#include <QPushButton>
#include <QTableWidget>
#include <QtTest>

void PreviewTest::tokensExposeCopyableValuesAndSources() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("tokens"));
    window.show();
    for (const auto* theme : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(theme);
        QVERIFY(host);
        auto* tokens = host->findChild<QTableWidget*>("previewTokens");
        QVERIFY(tokens);
        const auto spacing = tokens->findItems("spacing.0.25", Qt::MatchExactly);
        QCOMPARE(spacing.size(), 1);
        QCOMPARE(tokens->item(spacing.front()->row(), 1)->text(), QStringLiteral("1px"));
        tokens->scrollToItem(spacing.front());
        QTRY_VERIFY(tokens->viewport()->rect().contains(tokens->visualItemRect(spacing.front())));
    }
    auto* light = window.findChild<QWidget*>("previewLight");
    auto* table = light->findChild<QTableWidget*>("previewTokens");
    QVERIFY(table);
    const auto matches = table->findItems("color.background", Qt::MatchExactly);
    QCOMPARE(matches.size(), 1);
    table->setCurrentCell(matches.front()->row(), 0);
    auto* copy = light->findChild<QPushButton*>("previewCopyToken");
    QVERIFY(copy);
    copy->click();
    QVERIFY(QApplication::clipboard()->text().contains("background"));
    QVERIFY(QApplication::clipboard()->text().contains("#f6f7f8", Qt::CaseInsensitive));
    QVERIFY(QApplication::clipboard()->text().contains("desktop/design_system/tokens/tokens.cpp"));
}
