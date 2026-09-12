#pragma once
#include "models/shortcut_catalog.h"
#include <QDialog>
#include <QPointer>
class QCheckBox;
class QFontComboBox;
class QKeySequenceEdit;
class QLabel;
class QPushButton;
class QSpinBox;
namespace choscordb {
class SqlEditor;
class PreferencesDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit PreferencesDialog(EngineAdapter* adapter, QList<ShortcutDescriptor> catalog,
                               QWidget* parent = nullptr);

  signals:
    void preferencesSaveSubmitted(quint64 token);
    void preferencesConfirmed(const choscordb::EditorPreferences& preferences);

  private:
    EditorPreferences draft() const;
    void fill(const EditorPreferences& preferences);
    void updatePreview();
    void apply();
    void setBusy(bool busy);
    QPointer<EngineAdapter> adapter_;
    QList<ShortcutDescriptor> catalog_;
    QList<QKeySequenceEdit*> sequences_;
    QFontComboBox* font_;
    QCheckBox* system_;
    QSpinBox* size_;
    SqlEditor* preview_;
    QWidget* pages_;
    QLabel* status_;
    QPushButton *apply_, *reset_;
    quint64 token_ = 0;
    bool busy_ = false, ready_ = false, saving_ = false;
};
} // namespace choscordb
