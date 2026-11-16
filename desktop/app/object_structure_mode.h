#pragma once
#include <QKeySequence>
#include <QList>
#include <QPointer>
#include <QVariantList>
#include <QWidget>
#include <functional>
#include <optional>

class QAction;
class QTabBar;
namespace choscordb {
namespace design {
class Button;
}
class EngineAdapter;
class StructureEditor;
class ObjectStructureMode final : public QWidget {
    Q_OBJECT
  public:
    ObjectStructureMode(EngineAdapter* adapter, QTabBar* panes, QWidget* parent);
    QWidget* headerActions() const;
    void setDriverResolver(std::function<QString(quint64)> resolver);
    void setShortcuts(const QList<QKeySequence>& review, const QList<QKeySequence>& discard);
    void setObject(std::optional<quint64> connection, const QString& objectId, const QString& kind,
                   const QVariantList& properties, bool blocked);
    QString begin();
    bool resolvePendingChanges();

  signals:
    void structureChanged(quint64 connection);
    void statusChanged(const QString& message, bool error, bool busy);

  private:
    QString unavailableReason() const;
    void end();
    void review();
    void refresh();
    QPointer<EngineAdapter> adapter_;
    QTabBar* panes_;
    std::function<QString(quint64)> driverResolver_;
    std::optional<quint64> connection_;
    QString objectId_, unavailable_;
    bool blocked_ = false;
    bool relation_ = false;
    StructureEditor* editor_ = nullptr;
    design::Button* edit_ = nullptr;
    design::Button* review_ = nullptr;
    design::Button* discard_ = nullptr;
    QAction* reviewAction_ = nullptr;
    QAction* discardAction_ = nullptr;
};
}
