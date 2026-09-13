#pragma once

#include "design_system/theme.h"

#include <QMainWindow>
#include <QStringList>

class QComboBox;
class QLineEdit;
class QListWidget;
class QLabel;

namespace choscordb::design {

// Developer-only host; does not install a theme on QApplication or read preferences.
class PreviewWindow final : public QMainWindow {
    Q_OBJECT

  public:
    explicit PreviewWindow(QWidget* parent = nullptr);
    [[nodiscard]] QStringList visibleSections() const;
    [[nodiscard]] bool selectSection(const QString& section);
    [[nodiscard]] QStringList specimenIds() const;
    [[nodiscard]] bool selectSpecimen(const QString& id);
    [[nodiscard]] bool exportCapture(const QString& path, bool comparison = true,
                                     QSize logicalSize = {},
                                     ResolvedAppearance appearance = ResolvedAppearance::Light);

  private:
    void rebuildSpecimens();
    QComboBox* specimen_ = nullptr;
    QLineEdit* search_ = nullptr;
    QListWidget* navigation_ = nullptr;
    QWidget* comparison_ = nullptr;
    QWidget* light_ = nullptr;
    QWidget* dark_ = nullptr;
    QLabel* source_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace choscordb::design
