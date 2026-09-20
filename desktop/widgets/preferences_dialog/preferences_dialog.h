#pragma once
#include "design_system/dialog_shell/dialog_shell.h"
#include "models/shortcut_catalog.h"
#include <QPointer>
class QCheckBox;
class QFontComboBox;
class QKeySequenceEdit;
class QLabel;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
namespace choscordb {
namespace design {
class FieldValidation;
}
class SqlEditor;
class AppearanceController;
class PreferencesDialog final : public DialogShell {
    Q_OBJECT
  public:
    explicit PreferencesDialog(EngineAdapter* adapter, QList<ShortcutDescriptor> catalog,
                               QWidget* parent = nullptr,
                               AppearanceController* appearance = nullptr);

  signals:
    void preferencesSaveSubmitted(quint64 token);
    void preferencesConfirmed(const choscordb::EditorPreferences& preferences);
    void queryPreferencesSaveSubmitted(quint64 token);
    void queryPreferencesConfirmed(const choscordb::QueryPreferences& preferences);
    void historyPolicyConfirmed(const choscordb::HistoryPolicy& policy);

  protected:
    void reject() override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private:
    EditorPreferences draft() const;
    void fill(const EditorPreferences& preferences);
    void updatePreview();
    void updateAppearanceStatus();
    void apply();
    void setBusy(bool busy);
    void finishRequests();
    void fillQuery(const QueryPreferences& value);
    void fillHistory(const HistoryPolicy& value);
    bool placeValidationError(const QString& message);
    QPointer<EngineAdapter> adapter_;
    QPointer<AppearanceController> appearance_;
    QList<ShortcutDescriptor> catalog_;
    QList<QKeySequenceEdit*> sequences_;
    QList<design::FieldValidation*> sequenceValidations_;
    design::FieldValidation *fontValidation_, *sizeValidation_;
    QFontComboBox* font_;
    QCheckBox* system_;
    QSpinBox* size_;
    QSpinBox *pageSize_, *timeout_;
    QDoubleSpinBox *historyDays_, *historyRecords_;
    QCheckBox* recordHistory_;
    SqlEditor* preview_;
    QWidget* pages_;
    QLabel* status_;
    QLabel* appearanceStatus_ = nullptr;
    QString appearanceWarning_;
    bool forcedContrast_ = false;
    QComboBox* theme_ = nullptr;
    QPushButton *apply_, *reset_;
    quint64 token_ = 0, queryToken_ = 0, historyToken_ = 0;
    bool appearancePending_ = false;
    QStringList errors_;
    bool busy_ = false, ready_ = false, saving_ = false, appearanceValid_ = true;
};
} // namespace choscordb
