#pragma once

#include "design_system/dialog_presentation/dialog_presentation.h"

#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QString>

class QLabel;
class QLineEdit;
class QListWidget;

namespace choscordb::design {

struct QuickSearchResult final {
    QString type;
    QString title;
    QString context;
    QString id;
};

// Presentation and input only. The caller supplies result ordering and owns activation.
class QuickSearchDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit QuickSearchDialog(QWidget* parent);
    ~QuickSearchDialog() override;

    [[nodiscard]] QString query() const;
    [[nodiscard]] QList<QuickSearchResult> results() const;
    [[nodiscard]] QString selectedResultId() const;
    void setResults(const QList<QuickSearchResult>& results);
    void setStatus(const QString& message);
    void setLoading(bool loading);
    void setError(const QString& message);
    void setReopenShortcut(const QKeySequence& shortcut);

  public slots:
    void openSearch();

  signals:
    void queryChanged(const QString& query);
    void activated(const QString& id);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    void activateCurrent();
    void updateSelectedAccessibility();
    void updateStatus();

    DialogPresentation presentation_;
    QLineEdit* input_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* status_ = nullptr;
    QList<QuickSearchResult> results_;
    QString statusMessage_;
    QString errorMessage_;
    QKeySequence reopenShortcut_;
    bool loading_ = false;
};

[[nodiscard]] QString quickSearchStyleSheet();

} // namespace choscordb::design
