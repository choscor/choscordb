#pragma once

#include <QList>
#include <QString>
#include <memory>

namespace choscordb {

class TextFilter final {
  public:
    explicit TextFilter(const QString& query = {});
    ~TextFilter();
    TextFilter(TextFilter&&) noexcept;
    TextFilter& operator=(TextFilter&&) noexcept;
    [[nodiscard]] bool blank() const;
    [[nodiscard]] bool matches(const QString& text) const;

  private:
    struct Rust;
    std::unique_ptr<Rust> rust_;
};

struct TextSpan {
    qsizetype start = 0, length = 0;
    quint64 byteStart = 0, byteLength = 0;
};

class TextFinder final {
  public:
    explicit TextFinder(const QString& needle);
    ~TextFinder();
    TextFinder(const TextFinder&) = delete;
    TextFinder& operator=(const TextFinder&) = delete;
    [[nodiscard]] QList<TextSpan> findAll(const QString& source, int limit) const;

  private:
    struct Rust;
    std::unique_ptr<Rust> rust_;
};

}
