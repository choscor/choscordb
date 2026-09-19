#include "design_system/style/style_resource.h"

#include <QDebug>
#include <QFile>

static void ensureStyleResources() {
    static const bool registered = [] {
        Q_INIT_RESOURCE(styles);
        return true;
    }();
    Q_UNUSED(registered);
}

namespace choscordb::design {
QString loadStyleSheet(QStringView relativePath) {
    ensureStyleResources();
    const QString path = QStringLiteral(":/styles/") + relativePath.toString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot load stylesheet" << path << file.errorString();
        return {};
    }
    return QString::fromUtf8(file.readAll());
}
} // namespace choscordb::design
