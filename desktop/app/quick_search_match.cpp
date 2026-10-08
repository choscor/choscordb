#include "app/quick_search_match.h"
#include <QStringList>
#include <algorithm>

namespace choscordb {
namespace {
QStringList words(const QString& text) {
    QStringList result;
    QString current;
    for (const QChar character : text) {
        if (character.isLetterOrNumber()) {
            current += character;
        } else if (!current.isEmpty()) {
            result.append(current);
            current.clear();
        }
    }
    if (!current.isEmpty())
        result.append(current);
    return result;
}
} // namespace

QuickSearchNeedle::QuickSearchNeedle(const QString& query)
    : valid_(query.size() <= 128 && query.isValidUtf16()),
      folded_(valid_ ? query.trimmed().toCaseFolded() : QString{}), words_(words(folded_)) {}

std::optional<int> QuickSearchNeedle::score(const QString& name) const {
    if (!valid_ || name.size() > 1024 || !name.isValidUtf16())
        return std::nullopt;
    const QString& needle = folded_;
    const QString haystack = name.toCaseFolded();
    if (needle.isEmpty() || haystack.isEmpty())
        return std::nullopt;
    if (needle == haystack)
        return 0;
    if (haystack.startsWith(needle))
        return 10 + int(std::min<qsizetype>(9, (haystack.size() - needle.size()) / 8));
    const auto substring = haystack.indexOf(needle);
    if (substring >= 0)
        return 25 + int(std::min<qsizetype>(14, substring / 4));

    const auto& parts = words_;
    if (parts.size() > 1) {
        qsizetype position = 0;
        qsizetype gaps = 0;
        qsizetype first = -1;
        bool matched = true;
        for (const auto& part : parts) {
            const auto found = haystack.indexOf(part, position);
            if (found < 0) {
                matched = false;
                break;
            }
            if (first < 0)
                first = found;
            gaps += found - position;
            position = found + part.size();
        }
        if (matched)
            return 45 + int(std::min<qsizetype>(19, first / 4 + gaps / 4));
    }

    qsizetype position = 0;
    qsizetype first = -1;
    qsizetype last = -1;
    for (const QChar character : needle) {
        if (!character.isLetterOrNumber())
            continue;
        const auto found = haystack.indexOf(character, position);
        if (found < 0)
            return std::nullopt;
        if (first < 0)
            first = found;
        last = found;
        position = found + 1;
    }
    if (first < 0)
        return std::nullopt;
    return 70 + int(std::min<qsizetype>(29, first / 4 + (last - first) / 4));
}

std::optional<int> quickSearchNameScore(const QString& query, const QString& name) {
    return QuickSearchNeedle(query).score(name);
}

} // namespace choscordb
