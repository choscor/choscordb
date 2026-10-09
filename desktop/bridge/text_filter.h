#pragma once

#include <QList>
#include <QString>
#include <memory>

namespace choscordb {

// A list filter query folded once in Rust so it can test many visible labels.
class TextFilter final {
  public:
    explicit TextFilter(const QString& query = {});
    ~TextFilter();
    TextFilter(TextFilter&&) noexcept;
    TextFilter& operator=(TextFilter&&) noexcept;
    // True when the query is blank and every label is accepted.
    [[nodiscard]] bool blank() const;
    [[nodiscard]] bool matches(const QString& text) const;

  private:
    struct Rust;
    std::unique_ptr<Rust> rust_;
};

// A match in both QString positions and UTF-8 byte offsets (editor positions).
struct TextSpan {
    qsizetype start = 0, length = 0;
    quint64 byteStart = 0, byteLength = 0;
};

// A literal case-insensitive search compiled once in Rust and run on many texts.
class TextFinder final {
  public:
    explicit TextFinder(const QString& needle);
    ~TextFinder();
    TextFinder(const TextFinder&) = delete;
    TextFinder& operator=(const TextFinder&) = delete;
    // Up to `limit` matches in `source`; none for a blank or unusable needle.
    [[nodiscard]] QList<TextSpan> findAll(const QString& source, int limit) const;

  private:
    struct Rust;
    std::unique_ptr<Rust> rust_;
};

} // namespace choscordb
