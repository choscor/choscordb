#pragma once
#include <QByteArray>
#include <QList>
#include <QString>

namespace choscordb {
struct JsonPreviewNode {
    qsizetype parent = -1;
    QString key, value, kind;
};
struct ValuePreview {
    QString kind, summary, pretty, minified, imageFormat;
    QList<JsonPreviewNode> nodes;
    QByteArray pageImage;
};
}
