#include "value_preview_model.h"
#include "bridge/engine_adapter.h"
#include "bridge/rust_text.h"
#include "choscordb-bridge/src/lib.rs.h"
#include <QStringDecoder>
namespace choscordb {
bool ValuePreviewModel::setChunk(QByteArray bytes, quint64 offset, quint64 totalBytes,
                                 bool binary) {
    if (quint64(bytes.size()) > EngineAdapter::valueChunkBytes() || offset > totalBytes ||
        static_cast<quint64>(bytes.size()) > totalBytes - offset)
        return false;
    // Own only the requested window, even if the caller sliced a larger allocation.
    QByteArray owned(bytes.constData(), bytes.size());
    // Rust splits rows without breaking UTF-8 characters and holds back a partial tail.
    const auto rows = value_preview_rows(bridge_detail::byteView(owned), binary,
                                         offset + static_cast<quint64>(owned.size()) < totalBytes,
                                         binary ? 16 : 256);
    std::vector<quint32> boundaries(rows.boundaries.begin(), rows.boundaries.end());
    const auto end = static_cast<qsizetype>(rows.end);
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
