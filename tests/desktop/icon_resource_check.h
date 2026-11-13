#pragma once

#include "design_system/icons.h"

#include <QFile>
#include <QSvgRenderer>
#include <QXmlStreamReader>

int qInitResources_resources();

namespace choscordb::test {

inline bool iconResourceDecodes(design::Icon icon) {
    ::qInitResources_resources();
    const auto path = design::iconResourcePath(icon);
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly))
        return false;
    const auto svg = source.readAll();
    QXmlStreamReader reader(svg);
    bool root = false;
    bool drawingElement = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;
        if (!root) {
            root = reader.name() == QLatin1String("svg") && !QSvgRenderer(svg).viewBoxF().isEmpty();
        } else if (reader.name() == QLatin1String("path") ||
                   reader.name() == QLatin1String("polygon") ||
                   reader.name() == QLatin1String("rect") ||
                   reader.name() == QLatin1String("circle") ||
                   reader.name() == QLatin1String("ellipse") ||
                   reader.name() == QLatin1String("line") ||
                   reader.name() == QLatin1String("polyline")) {
            drawingElement = true;
        }
    }
    return !reader.hasError() && root && drawingElement;
}

}
