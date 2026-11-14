#pragma once

#include <QDialog>
#include <QHash>
#include <QString>
#include <optional>

class QLabel;
class QResizeEvent;
class QShowEvent;

namespace choscordb {
namespace design {
class DialogPresentation;
class DialogSections;
}

class DialogShell : public QDialog {
    Q_OBJECT

  public:
    explicit DialogShell(QWidget* parent = nullptr);
    void setAppModal();
    void open() override;
    [[nodiscard]] design::DialogSections* createSections();
    [[nodiscard]] QLabel* createDescription(const QString& text, QWidget* parent);
    [[nodiscard]] QLabel* createInlineStatus(QWidget* parent);
    void markDraftClean();
    [[nodiscard]] virtual bool hasDraft() const;
    void closeWithSuccess(const QString& title, const QString& detail);
    void reject() override;

  protected:
    [[nodiscard]] bool confirmDiscardDraft();
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

  private:
    void ensureContentHeight();
    design::DialogPresentation* presentation_ = nullptr;
    std::optional<QHash<const QWidget*, QString>> cleanDraft_;
};

}
