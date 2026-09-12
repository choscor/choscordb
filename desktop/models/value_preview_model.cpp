#include "value_preview_model.h"
#include <QStringDecoder>
#include <algorithm>
namespace choscordb {
namespace {
bool continuation(char byte) {
    return (static_cast<unsigned char>(byte) & 0xc0) == 0x80;
}
int sequenceSize(char byte) {
    const auto value = static_cast<unsigned char>(byte);
    if (value >= 0xc2 && value <= 0xdf)
        return 2;
    if (value >= 0xe0 && value <= 0xef)
        return 3;
    if (value >= 0xf0 && value <= 0xf4)
        return 4;
    return 1;
}
} // namespace
bool ValuePreviewModel::setChunk(QByteArray bytes, quint64 offset, quint64 totalBytes,
                                 bool binary) {
    if (bytes.size() > MaxChunkBytes || offset > totalBytes ||
        static_cast<quint64>(bytes.size()) > totalBytes - offset)
        return false;
    // Own only the requested window, even if the caller sliced a larger allocation.
    QByteArray owned(bytes.constData(), bytes.size());
    qsizetype begin = 0;
    qsizetype end = owned.size();
    if (!binary) {
        if (offset + static_cast<quint64>(end) < totalBytes && begin < end) {
            auto lead = end - 1;
            while (lead > begin && continuation(owned[lead]))
                --lead;
            if (sequenceSize(owned[lead]) > end - lead)
                end = lead;
        }
    }
    std::vector<quint32> boundaries;
    if (begin < end) {
        const qsizetype width = binary ? 16 : 256;
        boundaries.reserve(static_cast<std::size_t>((end - begin) / (binary ? 16 : 253) + 2));
        boundaries.push_back(static_cast<quint32>(begin));
        while (begin < end) {
            auto next = std::min(begin + width, end);
            if (!binary && next < end) {
                // At most three continuation bytes belong to a valid UTF-8 character.
                auto lead = next;
                for (int count = 0; count < 3 && lead > begin && continuation(owned[lead]); ++count)
                    --lead;
                if (lead < next && sequenceSize(owned[lead]) > next - lead)
                    next = lead;
            }
            boundaries.push_back(static_cast<quint32>(next));
            begin = next;
        }
    }
    beginResetModel();
    bytes_ = std::move(owned);
    boundaries_ = std::move(boundaries);
    offset_ = offset;
    nextOffset_ = offset + static_cast<quint64>(end);
    binary_ = binary;
    endResetModel();
    return true;
}
void ValuePreviewModel::clear() {
    beginResetModel();
    bytes_ = QByteArray();
    std::vector<quint32>().swap(boundaries_);
    offset_ = nextOffset_ = 0;
    endResetModel();
}
std::size_t ValuePreviewModel::residentBytes() const {
    return static_cast<std::size_t>(bytes_.capacity()) + (bytes_.isNull() ? 0 : 1) +
           boundaries_.capacity() * sizeof(quint32);
}
int ValuePreviewModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() || boundaries_.empty() ? 0 : static_cast<int>(boundaries_.size() - 1);
}
int ValuePreviewModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 2;
}
QVariant ValuePreviewModel::data(const QModelIndex& index, int role) const {
    if (role != Qt::DisplayRole || !index.isValid() || index.model() != this || index.row() < 0 ||
        index.row() >= rowCount() || index.column() < 0 || index.column() >= 2)
        return {};
    const auto start = boundaries_[static_cast<std::size_t>(index.row())];
    if (index.column() == 0)
        return QVariant::fromValue(offset_ + start);
    const auto size = boundaries_[static_cast<std::size_t>(index.row()) + 1] - start;
    const auto view = QByteArrayView(bytes_.constData() + start, size);
    if (binary_)
        return QString::fromLatin1(QByteArray(view.data(), view.size()).toHex(' '));
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString decoded = decoder(view);
    if (decoder.hasError())
        return QStringLiteral("[Invalid UTF-8; hex] ") +
               QString::fromLatin1(QByteArray(view.data(), view.size()).toHex(' '));
    QString escaped;
    escaped.reserve(decoded.size());
    for (const QChar character : decoded) {
        switch (character.unicode()) {
        case '\\':
            escaped += QStringLiteral("\\\\");
            break;
        case '\n':
            escaped += QStringLiteral("\\n");
            break;
        case '\r':
            escaped += QStringLiteral("\\r");
            break;
        case '\t':
            escaped += QStringLiteral("\\t");
            break;
        case 0:
            escaped += QStringLiteral("\\0");
            break;
        default:
            if (character.unicode() < 0x20 || character.unicode() == 0x7f ||
                character.unicode() == 0x2028 || character.unicode() == 0x2029)
                escaped += QStringLiteral("\\u%1").arg(static_cast<uint>(character.unicode()), 4,
                                                       16, QChar('0'));
            else
                escaped += character;
        }
    }
    return escaped;
}
QVariant ValuePreviewModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    if (section == 0)
        return QStringLiteral("Byte offset");
    if (section == 1)
        return binary_ ? QStringLiteral("Hexadecimal") : QStringLiteral("Text (escaped UTF-8)");
    return {};
}
} // namespace choscordb
