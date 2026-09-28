#include "app/quick_search_match.h"
#include <QCoreApplication>
#include <QDebug>
#include <cstdlib>

using choscordb::quickSearchNameScore;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto exact = quickSearchNameScore("sales orders", "Sales Orders");
    const auto prefix = quickSearchNameScore("sales", "Sales Orders");
    const auto substring = quickSearchNameScore("orders", "Sales Orders");
    const auto words = quickSearchNameScore("sales ord", "Sales_Orders");
    const auto subsequence = quickSearchNameScore("srdr", "Sales_Orders");
    if (!exact || !prefix || !substring || !words || !subsequence || *exact != 0 ||
        !(*exact < *prefix && *prefix < *substring && *substring < *words &&
          *words < *subsequence && *subsequence < 100)) {
        qCritical() << "Quick-search name ranking is incorrect";
        return EXIT_FAILURE;
    }
    if (quickSearchNameScore("orders sales", "Sales Orders") ||
        quickSearchNameScore("sales xyz", "Sales Orders") ||
        quickSearchNameScore("", "Sales Orders") ||
        quickSearchNameScore(QString(129, QChar('x')), "Sales Orders") ||
        quickSearchNameScore("sales", QString(1025, QChar('x')))) {
        qCritical() << "Quick-search name matching accepted an unrelated or unbounded input";
        return EXIT_FAILURE;
    }
    if (!quickSearchNameScore("  sales   orders  ", "Sales.Orders")) {
        qCritical() << "Separated query words should match in order";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
