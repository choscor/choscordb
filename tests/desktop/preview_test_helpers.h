#pragma once

#include <QImage>
#include <QWidget>

inline QImage visibleSurfaceSnapshot(QWidget& widget) {
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget.render(&image);
    return image.convertToFormat(QImage::Format_ARGB32);
}
