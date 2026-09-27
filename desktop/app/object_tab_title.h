#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QString>

namespace choscordb {
inline QString objectTabTitle(const QString& objectId, const QString& qualifiedLabel) {
    const auto document = QJsonDocument::fromJson(objectId.toUtf8());
    if (document.isArray()) {
        const auto parts = document.array();
        if (!parts.isEmpty() && parts.last().isString() && !parts.last().toString().isEmpty())
            return parts.last().toString();
    }

    // PostgreSQL uses opaque object IDs, so its display name comes from the SQL label.
    int separator = -1;
    QChar quote;
    for (int i = 0; i < qualifiedLabel.size(); ++i) {
        const auto character = qualifiedLabel.at(i);
        if (character == QLatin1Char('"') || character == QLatin1Char('`')) {
            if (quote == character && i + 1 < qualifiedLabel.size() &&
                qualifiedLabel.at(i + 1) == character) {
                ++i;
            } else if (quote.isNull()) {
                quote = character;
            } else if (quote == character) {
                quote = {};
            }
        } else if (character == QLatin1Char('.') && quote.isNull()) {
            separator = i;
        }
    }
    auto title = qualifiedLabel.mid(separator + 1);
    if (title.size() >= 2 && title.front() == title.back() &&
        (title.front() == QLatin1Char('"') || title.front() == QLatin1Char('`'))) {
        const auto delimiter = title.front();
        title = title.mid(1, title.size() - 2);
        title.replace(QString(delimiter).repeated(2), QString(delimiter));
    }
    return title;
}
} // namespace choscordb
