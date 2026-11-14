#pragma once

#include <QColor>

namespace choscordb::test {

inline bool blendedInk(const QColor& sample, const QColor& ink, const QColor& background) {
    const double inkRed = ink.red() - background.red();
    const double inkGreen = ink.green() - background.green();
    const double inkBlue = ink.blue() - background.blue();
    const double length = inkRed * inkRed + inkGreen * inkGreen + inkBlue * inkBlue;
    if (length == 0)
        return false;
    const double red = sample.red() - background.red();
    const double green = sample.green() - background.green();
    const double blue = sample.blue() - background.blue();
    const double coverage = (red * inkRed + green * inkGreen + blue * inkBlue) / length;
    const double residual = qAbs(red - coverage * inkRed) + qAbs(green - coverage * inkGreen) +
                            qAbs(blue - coverage * inkBlue);
    return coverage > 0.3 && residual < 40;
}

}
