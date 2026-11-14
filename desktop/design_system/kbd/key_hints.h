#pragma once

#include <QList>
#include <QString>
#include <QWidget>

namespace choscordb::design {

struct KeyHint final {
    QString keys;
    QString action;
};

class KeyHints final : public QWidget {
    Q_OBJECT
  public:
    explicit KeyHints(const QList<KeyHint>& hints, QWidget* parent = nullptr);
};

}
