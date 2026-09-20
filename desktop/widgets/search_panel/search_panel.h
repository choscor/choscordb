#pragma once
#include <QPointer>
#include <QWidget>
#include <functional>
class QCheckBox;
class QLabel;
class QLineEdit;
namespace choscordb {
namespace design {
class Button;
class FieldValidation;
} // namespace design
class SqlEditor;
class SearchPanel final : public QWidget {
    Q_OBJECT
  public:
    explicit SearchPanel(std::function<SqlEditor*()> currentEditor, QWidget* parent = nullptr);
    void showFind();
    void showReplace();
    void findNext();
    void findPrevious();
    void editorChanged();

  protected:
    void hideEvent(QHideEvent* event) override;

  private:
    void find(bool backwards);
    void replaceOne();
    void replaceAll();
    void invalidate();
    SqlEditor* editable(bool mutation = true) const;
    void setPending(bool pending);
    std::function<SqlEditor*()> currentEditor_;
    QLineEdit *needle_, *replacement_;
    design::FieldValidation *needleValidation_, *replacementValidation_;
    QCheckBox *case_, *word_;
    QWidget* replacementRow_;
    QLabel* status_;
    design::Button* replaceAll_;
    QPointer<SqlEditor> matchedEditor_;
    quint64 matchRevision_ = 0, matchStart_ = 0, matchEnd_ = 0, generation_ = 0;
    bool hasMatch_ = false, pending_ = false;
};
} // namespace choscordb
