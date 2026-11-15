#pragma once
#include <QArrayData>
#include <QByteArray>
#include <QString>
#include <algorithm>
#include <cstddef>

namespace choscordb::result_model_detail {
constexpr std::size_t QtHeaderBytes = sizeof(QArrayData) + alignof(std::max_align_t);
struct AllocationCounter {
    std::size_t limit;
    std::size_t bytes = 0;
    bool add(std::size_t count, std::size_t width = 1) {
        if (width == 0)
            return true;
        if (count > (limit - bytes) / width)
            return false;
        bytes += count * width;
        return true;
    }
    bool string(const QString& text) {
        return text.isNull() || (add(QtHeaderBytes) &&
                                 add(std::max(text.capacity(), text.size()) + 1, sizeof(QChar)));
    }
    bool binary(const QByteArray& value) {
        return value.isNull() ||
               (add(QtHeaderBytes) && add(std::max(value.capacity(), value.size()) + 1));
    }
};
}
