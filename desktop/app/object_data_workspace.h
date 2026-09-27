#pragma once
#include <QPointer>
#include <QString>
#include <QWidget>
namespace choscordb {
class QueryWorkspace;
class ObjectDataWorkspace final : public QWidget {
    Q_OBJECT
  public:
    explicit ObjectDataWorkspace(QueryWorkspace* sqlWorkspace, QWidget* parent = nullptr);
    void openObject(quint64 connection, const QString& object, const QString& label,
                    const QString& kind = QStringLiteral("table"));
    void invalidate();
    void setInitialFilter(const QString& expression) { initialFilter_ = expression; }
    bool resolvePendingEdits();
    QWidget* footerWidget() const { return footer_; }
    QWidget* toolbarWidget() const { return toolbar_; }
  signals:
    void busyChanged(bool busy);
    void foreignKeyRequested(quint64 connection, const QString& object, const QString& label,
                             const QString& filter);

  private:
    QPointer<QueryWorkspace> sql_;
    QueryWorkspace* result_ = nullptr;
    QWidget* footer_ = nullptr;
    QWidget* toolbar_ = nullptr;
    QString initialFilter_;
};
} // namespace choscordb
