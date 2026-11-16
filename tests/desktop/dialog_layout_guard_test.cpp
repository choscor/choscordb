#include "design_system/button/button.h"
#include "dialog_layout_guard.h"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>
#include <QtTest>

class DialogLayoutGuardTest : public QObject {
    Q_OBJECT

    static QWidget* buildWindow(QWidget& window) {
        window.setObjectName("fixture");
        auto* layout = new QVBoxLayout(&window);
        auto* label = new QLabel("Make a folder for saved queries in this workspace.", &window);
        label->setObjectName("description");
        label->setWordWrap(true);
        label->setMaximumHeight(label->fontMetrics().height());
        layout->addWidget(label);
        auto* rows = new QWidget;
        auto* rowLayout = new QVBoxLayout(rows);
        auto* row = new QLabel("first row");
        row->setObjectName("firstRow");
        row->setMinimumHeight(120);
        rowLayout->addWidget(row);
        rowLayout->addWidget(new QLabel("second row"));
        auto* scroll = new QScrollArea(&window);
        scroll->setObjectName("changes");
        scroll->setWidgetResizable(true);
        scroll->setWidget(rows);
        layout->addWidget(scroll, 1);
        auto* preview = new QLabel("preview", &window);
        preview->setMinimumHeight(220);
        layout->addWidget(preview);
        auto* accept = new QPushButton("Create folder", &window);
        accept->setObjectName("accept");
        layout->addWidget(accept);
        return accept;
    }

  private slots:
    void squeezedControlsAreReported() {
        QWidget window;
        auto* accept = buildWindow(window);
        QVERIFY(accept);
        window.layout()->setSizeConstraint(QLayout::SetNoConstraint);
        window.resize(80, 330);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE_LT(accept->width(), accept->sizeHint().width());
        const auto problems = choscordb::test::dialogLayoutProblems(window);
        const auto has = [&problems](const QString& fragment) {
            for (const auto& problem : problems)
                if (problem.contains(fragment))
                    return true;
            return false;
        };
        QVERIFY2(has("QPushButton accept \"Create folder\" is "), qPrintable(problems.join('\n')));
        QVERIFY2(has("QScrollArea changes shows"), qPrintable(problems.join('\n')));
        QVERIFY2(has("QLabel description"), qPrintable(problems.join('\n')));
    }

    void twoPrimaryButtonsAreReported() {
        QWidget window;
        window.setObjectName("fixture");
        auto* layout = new QHBoxLayout(&window);
        auto* ok = new choscordb::design::Button("OK", &window);
        ok->setObjectName("ok");
        auto* apply = new choscordb::design::Button("Apply changes", &window);
        apply->setObjectName("apply");
        auto* cancel = new choscordb::design::Button("Cancel", &window);
        cancel->setVariant(choscordb::design::ButtonVariant::Outline);
        for (auto* button : {ok, apply, cancel})
            layout->addWidget(button);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(choscordb::test::dialogLayoutProblems(window),
                 QStringList({"fixture: shows 2 primary buttons: choscordb::design::Button ok "
                              "\"OK\", choscordb::design::Button apply \"Apply changes\""}));
        apply->setVariant(choscordb::design::ButtonVariant::Outline);
        QVERIFY(choscordb::test::dialogLayoutProblems(window).isEmpty());
    }

    void overlappingSiblingFieldsAreReported() {
        QWidget window;
        window.setObjectName("fixture");
        window.resize(300, 200);
        auto* delimiter = new QLineEdit(&window);
        delimiter->setObjectName("delimiter");
        delimiter->setGeometry(10, 10, 200, 36);
        auto* quote = new QLineEdit(&window);
        quote->setObjectName("quote");
        quote->setGeometry(10, 44, 200, 36);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(choscordb::test::dialogLayoutProblems(window),
                 QStringList({"fixture: QLineEdit delimiter overlaps QLineEdit quote"}));
        quote->move(10, 50);
        QVERIFY(choscordb::test::dialogLayoutProblems(window).isEmpty());
    }

    void itemViewShorterThanItsFirstRowIsReported() {
        QWidget window;
        window.setObjectName("fixture");
        auto* layout = new QVBoxLayout(&window);
        QStandardItemModel model(2, 1);
        model.setItem(0, 0, new QStandardItem("ann"));
        auto* preview = new QTableView(&window);
        preview->setObjectName("preview");
        preview->setModel(&model);
        preview->verticalHeader()->setDefaultSectionSize(45);
        preview->setFixedHeight(preview->horizontalHeader()->sizeHint().height() + 30);
        layout->addWidget(preview);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto problems = choscordb::test::dialogLayoutProblems(window);
        QCOMPARE(problems.size(), 1);
        QVERIFY2(problems.first().startsWith("fixture: QTableView preview shows "),
                 qPrintable(problems.join('\n')));
        preview->setFixedHeight(preview->horizontalHeader()->sizeHint().height() + 60);
        QTRY_VERIFY(choscordb::test::dialogLayoutProblems(window).isEmpty());
    }

    void roomyWindowHasNoProblems() {
        QWidget window;
        buildWindow(window);
        window.resize(600, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto problems = choscordb::test::dialogLayoutProblems(window);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
    }
};

QTEST_MAIN(DialogLayoutGuardTest)
#include "dialog_layout_guard_test.moc"
