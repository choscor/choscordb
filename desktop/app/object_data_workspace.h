#pragma once
#include <QPointer>
#include <QString>
#include <QWidget>
class QComboBox;
class QLineEdit;
namespace choscordb {
class QueryWorkspace;
struct ObjectDataTime;
class ObjectDataWorkspace final : public QWidget {
    Q_OBJECT
  public:
    explicit ObjectDataWorkspace(QueryWorkspace* sqlWorkspace, QWidget* parent = nullptr);
    void openObject(quint64 connection, const QString& object, const QString& label,
                    const QString& relationSubtype = {});
    void invalidate(bool connectionLost = false);
    void setInitialFilter(const QString& expression) { initialFilter_ = expression; }
    bool resolvePendingEdits();
    QWidget* footerWidget() const { return footer_; }
    QWidget* toolbarWidget() const { return toolbar_; }
  signals:
    void busyChanged(bool busy);
    void foreignKeyRequested(quint64 connection, const QString& object, const QString& label,
                             const QString& filter);

  private:
    bool canRead() const;
    ObjectDataTime time() const;
    bool read(const QString& filter);
    void readSelectedTime();
    QPointer<QueryWorkspace> sql_;
    QueryWorkspace* result_ = nullptr;
    QWidget* footer_ = nullptr;
    QWidget* toolbar_ = nullptr;
    QString initialFilter_;
    QWidget* history_ = nullptr;
    QComboBox* timeMode_ = nullptr;
    QLineEdit* asOf_ = nullptr;
    // The selector entry whose rows are shown, restored when a read does not start.
    int shownTimeMode_ = 0;
    quint64 connection_ = 0;
    QString object_, label_;
};
} // namespace choscordb
