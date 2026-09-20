#pragma once
#include <QPointer>
#include <QWidget>
class QPushButton;
namespace choscordb {
class QueryWorkspace;
class ObjectDataWorkspace final : public QWidget {
    Q_OBJECT
  public:
    explicit ObjectDataWorkspace(QueryWorkspace* sqlWorkspace, QWidget* parent = nullptr);
    void openObject(quint64 connection, const QString& object, const QString& label,
                    const QString& kind = QStringLiteral("table"));
    void invalidate();
    bool resolvePendingEdits();
    QWidget* footerWidget() const { return footer_; }
    QWidget* toolbarWidget() const { return toolbar_; }
  signals:
    void busyChanged(bool busy);

  private:
    QPointer<QueryWorkspace> sql_;
    QueryWorkspace* result_ = nullptr;
    QPushButton* refresh_ = nullptr;
    QWidget* footer_ = nullptr;
    QWidget* toolbar_ = nullptr;
    quint64 connection_ = 0;
    QString object_, label_, kind_;
};
} // namespace choscordb
