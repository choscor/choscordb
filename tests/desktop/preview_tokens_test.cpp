#include "preview_test.h"
#include "tools/preview/preview_window.h"

#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
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
        const auto spacing = tokens->findItems("spacing.0.5", Qt::MatchExactly);
        QCOMPARE(spacing.size(), 1);
        QCOMPARE(tokens->item(spacing.front()->row(), 1)->text(), QStringLiteral("2px"));
        tokens->scrollToItem(spacing.front());
        QTRY_VERIFY(tokens->viewport()->rect().contains(tokens->visualItemRect(spacing.front())));
        const auto source = tokens->item(spacing.front()->row(), 2);
        QCOMPARE(source->toolTip(), source->text());
        QCOMPARE(tokens->horizontalHeader()->sectionResizeMode(2), QHeaderView::Stretch);
        const auto sourceRight = tokens->visualItemRect(source).right();
        QVERIFY2(sourceRight <= tokens->viewport()->width(),
                 qPrintable(QString::number(sourceRight)));
    }
    auto* light = window.findChild<QWidget*>("previewLight");
    auto* table = light->findChild<QTableWidget*>("previewTokens");
    QVERIFY(table);
    const auto matches = table->findItems("color.kumo-canvas", Qt::MatchExactly);
    QCOMPARE(matches.size(), 1);
    table->setCurrentCell(matches.front()->row(), 0);
    auto* copy = light->findChild<QPushButton*>("previewCopyToken");
    QVERIFY(copy);
    copy->click();
    QVERIFY(QApplication::clipboard()->text().contains("color.kumo-canvas"));
    QVERIFY(QApplication::clipboard()->text().contains("#fbfbfb", Qt::CaseInsensitive));
    QVERIFY(QApplication::clipboard()->text().contains("desktop/design_system/tokens/tokens.cpp"));
}
