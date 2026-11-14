#include "control_style_test.h"
#include "design_system/metrics/metrics.h"
#include "design_system/theme_manager.h"
#include <QListWidget>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtMath>
#include <QtTest>

namespace {
int firstChangedColumn(const QImage& withText, const QImage& withoutText) {
    for (int x = 0; x < withText.width(); ++x)
        for (int y = 0; y < withText.height(); ++y)
            if (withText.pixel(x, y) != withoutText.pixel(x, y))
                return x;
    return -1;
}
}

void ControlStyleTest::renderedItemTextKeepsTheItemPadding_data() {
    QTest::addColumn<bool>("tree");
    QTest::addColumn<choscordb::design::Spacing>("leftPadding");
    QTest::newRow("list") << false << choscordb::design::Spacing::OneHalf;
    QTest::newRow("tree") << true << choscordb::design::Spacing::One;
}

void ControlStyleTest::renderedItemTextKeepsTheItemPadding() {
    using namespace choscordb::design;
    QFETCH(bool, tree);
    QFETCH(Spacing, leftPadding);
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    auto* layout = new QVBoxLayout(&root);
    QAbstractItemView* view = nullptr;
    QListWidgetItem* listItem = nullptr;
    QTreeWidgetItem* treeItem = nullptr;
    if (tree) {
        auto* widget = new QTreeWidget(&root);
        widget->setHeaderHidden(true);
        widget->setRootIsDecorated(false);
        widget->setIndentation(0);
        treeItem = new QTreeWidgetItem(widget, {"Wireless mouse"});
        view = widget;
    } else {
        auto* widget = new QListWidget(&root);
        listItem = new QListWidgetItem("Wireless mouse", widget);
        view = widget;
    }
    layout->addWidget(view);
    root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    const auto cell = view->visualRect(view->model()->index(0, 0));
    const auto withText = view->viewport()->grab(cell).toImage();
    if (tree)
        treeItem->setText(0, {});
    else
        listItem->setText({});
    QCoreApplication::processEvents();
    const int changedColumn = firstChangedColumn(withText, view->viewport()->grab(cell).toImage());
    QVERIFY(changedColumn >= 0);
    const int textLeft = qFloor(changedColumn / withText.devicePixelRatio());
    const int textMargin = view->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, view);
    QVERIFY2(textLeft >= spacing(leftPadding) + textMargin, qPrintable(QString::number(textLeft)));
}
