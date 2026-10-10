#pragma once

#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/icons.h"

#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;

namespace choscordb::design {

struct QuickSearchResult final {
    QString type;
    QString title;
    QString context;
    QString id;
    Icon icon = Icon::Search;
};

// Shared presentation for every destination returned by quick search.
class QuickSearchResultRow final : public QWidget {
  public:
    explicit QuickSearchResultRow(const QuickSearchResult& result, QWidget* parent = nullptr);
    void setSelected(bool selected);
    void refreshAppearance();

  protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private:
    Icon iconRole_;
    QLabel* iconLabel_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* detailLabel_ = nullptr;
    bool selected_ = false;
    bool refreshing_ = false;
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
    void changeEvent(QEvent* event) override;
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

} // namespace choscordb::design
