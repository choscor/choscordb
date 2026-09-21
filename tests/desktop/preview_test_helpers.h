#pragma once

#include <QImage>
#include <QVariant>
#include <QWidget>

inline QImage visibleSurfaceSnapshot(QWidget& widget) {
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget.render(&image);
    return image.convertToFormat(QImage::Format_ARGB32);
}

template <typename T> T* previewSurface(QWidget* host, const char* trigger = "previewOpenDialog") {
    auto* control = host->findChild<QWidget*>(QString::fromLatin1(trigger));
    return control
               ? qobject_cast<T*>(control->property("previewSurface").template value<QObject*>())
               : nullptr;
}
