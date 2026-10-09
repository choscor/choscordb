#include "bridge/text_filter.h"
#include "bridge/rust_text.h"

namespace choscordb {
using bridge_detail::utf8View;

struct TextFilter::Rust {
    rust::Box<TextFilterQuery> query;
};

TextFilter::TextFilter(const QString& query)
    : rust_(std::make_unique<Rust>(Rust{text_filter_query(utf8View(query.toUtf8()))})) {}

TextFilter::~TextFilter() = default;
TextFilter::TextFilter(TextFilter&&) noexcept = default;
TextFilter& TextFilter::operator=(TextFilter&&) noexcept = default;

bool TextFilter::blank() const {
    return rust_->query->blank();
}

bool TextFilter::matches(const QString& text) const {
    const auto bytes = text.toUtf8();
    return rust_->query->accepts(utf8View(bytes));
}

struct TextFinder::Rust {
    rust::Box<TextFinderQuery> finder;
};

TextFinder::TextFinder(const QString& needle)
    : rust_(std::make_unique<Rust>(
          Rust{text_finder(utf8View(needle.isValidUtf16() ? needle.toUtf8() : QByteArray{}))})) {}

TextFinder::~TextFinder() = default;

QList<TextSpan> TextFinder::findAll(const QString& source, int limit) const {
    if (limit <= 0 || !source.isValidUtf16())
        return {};
    const auto sourceBytes = source.toUtf8();
    QList<TextSpan> spans;
    quint64 byte = 0;
    qsizetype position = 0;
    const auto advance = [&](quint64 to) {
        position += QString::fromUtf8(sourceBytes.mid(static_cast<qsizetype>(byte),
                                                      static_cast<qsizetype>(to - byte)))
                        .size();
        byte = to;
        return position;
    };
    for (const auto& range :
         rust_->finder->find_all(utf8View(sourceBytes), static_cast<quint32>(limit))) {
        const auto start = advance(range.start);
        spans.append({start, advance(range.end) - start, range.start, range.end - range.start});
    }
    return spans;
}

} // namespace choscordb
