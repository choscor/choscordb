#pragma once
#include <QString>

class QWidget;
namespace choscordb {
struct SqlReview {
    QString objectPrefix, title, dismiss, description, sql, accept;
};
[[nodiscard]] bool confirmSqlReview(QWidget* parent, const SqlReview& review);
}
