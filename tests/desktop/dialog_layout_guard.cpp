#include "dialog_layout_guard.h"
#include "design_system/button/button.h"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QTabBar>
#include <QTimer>
#include <QtTest>

namespace choscordb::test {
namespace {
bool insideScrollViewport(const QWidget* widget, const QWidget* dialog) {
    for (const auto* ancestor = widget->parentWidget(); ancestor && ancestor != dialog;
         ancestor = ancestor->parentWidget()) {
        const auto* area = qobject_cast<const QAbstractScrollArea*>(ancestor->parentWidget());
        if (area && area->viewport() == ancestor)
            return true;
    }
    return false;
}

bool popupSurface(const QWidget* widget) {
    return widget->isWindow() || widget->property("embeddedPopupController").isValid();
}

bool ownedByOtherOwner(const QWidget* widget, const QWidget* dialog) {
    if (popupSurface(widget) || qobject_cast<const QDialog*>(widget))
        return true;
    for (const auto* ancestor = widget->parentWidget(); ancestor && ancestor != dialog;
         ancestor = ancestor->parentWidget()) {
        const auto* area = qobject_cast<const QAbstractScrollArea*>(ancestor);
        if (qobject_cast<const QLineEdit*>(ancestor) || qobject_cast<const QComboBox*>(ancestor) ||
            qobject_cast<const QAbstractSpinBox*>(ancestor) ||
            qobject_cast<const QTabBar*>(ancestor) || qobject_cast<const QDialog*>(ancestor) ||
            (area && !qobject_cast<const QScrollArea*>(area)) || popupSurface(ancestor))
            return true;
    }
    return false;
}

bool sizedControl(const QWidget* widget) {
    if (const auto* label = qobject_cast<const QLabel*>(widget))
        return !label->text().isEmpty() || !label->pixmap().isNull();
    return qobject_cast<const QAbstractButton*>(widget) || qobject_cast<const QLineEdit*>(widget) ||
           qobject_cast<const QComboBox*>(widget) || qobject_cast<const QAbstractSpinBox*>(widget);
}

int effectiveMinimum(int explicitMinimum, QSizePolicy::Policy policy, int hint) {
    if (explicitMinimum > 0)
        return explicitMinimum;
    return policy == QSizePolicy::Ignored ? 0 : hint;
}

QString describe(const QWidget* widget) {
    const auto name =
        widget->objectName().isEmpty() ? QStringLiteral("<unnamed>") : widget->objectName();
    QString text;
    if (const auto* label = qobject_cast<const QLabel*>(widget))
        text = label->text();
    else if (const auto* button = qobject_cast<const QAbstractButton*>(widget))
        text = button->text();
    return QStringLiteral("%1 %2%3").arg(
        QString::fromLatin1(widget->metaObject()->className()), name,
        text.isEmpty() ? QString() : QStringLiteral(" \"%1\"").arg(text.left(40)));
}

bool primaryButton(const QWidget* widget) {
    if (const auto* button = qobject_cast<const design::Button*>(widget))
        return button->variant() == design::ButtonVariant::Default;
    return qobject_cast<const QAbstractButton*>(widget) &&
           widget->property("variant").toString() == QLatin1String("default");
}

QWidget* firstContentRow(const QScrollArea* area) {
    const auto* content = area->widget();
    if (!content || !content->layout())
        return nullptr;
    for (int index = 0; index < content->layout()->count(); ++index) {
        auto* row = content->layout()->itemAt(index)->widget();
        if (row && row->isVisibleTo(content))
            return row;
    }
    return nullptr;
}
}

QStringList dialogLayoutProblems(const QWidget& dialog) {
    QStringList problems;
    const auto prefix = dialog.objectName() + QStringLiteral(": ");
    QStringList primaries;
    QList<const QWidget*> controls;
    for (auto* widget : dialog.findChildren<QWidget*>()) {
        if (!widget->isVisibleTo(&dialog) || ownedByOtherOwner(widget, &dialog))
            continue;
        if (const auto* area = qobject_cast<const QScrollArea*>(widget)) {
            const auto* row = firstContentRow(area);
            if (row && area->widget()->height() > area->viewport()->height() &&
                row->height() > area->viewport()->height())
                problems << prefix + QStringLiteral("%1 shows %2 px of its %3 px first row")
                                         .arg(describe(widget))
                                         .arg(area->viewport()->height())
                                         .arg(row->height());
        }
        if (const auto* view = qobject_cast<const QAbstractItemView*>(widget);
            view && view->model() && view->model()->rowCount(view->rootIndex()) > 0) {
            const int rowHeight =
                view->visualRect(view->model()->index(0, 0, view->rootIndex())).height();
            if (view->viewport()->height() < rowHeight)
                problems << prefix + QStringLiteral("%1 shows %2 px of its %3 px first row")
                                         .arg(describe(widget))
                                         .arg(view->viewport()->height())
                                         .arg(rowHeight);
        }
        const bool scrolled = insideScrollViewport(widget, &dialog);
        if (!scrolled &&
            !dialog.rect().contains(QRect(widget->mapTo(&dialog, QPoint()), widget->size())))
            problems << prefix +
                            QStringLiteral("%1 extends outside the dialog").arg(describe(widget));
        if (primaryButton(widget))
            primaries << describe(widget);
        if (!sizedControl(widget))
            continue;
        for (const auto* other : std::as_const(controls)) {
            if (other->parentWidget() == widget->parentWidget() &&
                other->geometry().intersects(widget->geometry()))
                problems << prefix + QStringLiteral("%1 overlaps %2")
                                         .arg(describe(other), describe(widget));
        }
        controls << widget;
        if (const auto* label = qobject_cast<const QLabel*>(widget); label && label->wordWrap()) {
            const int needed = label->heightForWidth(label->width());
            if (needed > label->height())
                problems << prefix + QStringLiteral("%1 needs %2 px height but has %3 px")
                                         .arg(describe(widget))
                                         .arg(needed)
                                         .arg(label->height());
            continue;
        }
        const bool showsFullText =
            qobject_cast<const QAbstractButton*>(widget) || qobject_cast<const QLabel*>(widget);
        const auto hint = showsFullText ? widget->sizeHint() : widget->minimumSizeHint();
        const auto policy = widget->sizePolicy();
        const int minimumWidth = effectiveMinimum(showsFullText ? 0 : widget->minimumWidth(),
                                                  policy.horizontalPolicy(), hint.width());
        const int minimumHeight = effectiveMinimum(showsFullText ? 0 : widget->minimumHeight(),
                                                   policy.verticalPolicy(), hint.height());
        if (widget->width() < minimumWidth || widget->height() < minimumHeight)
            problems << prefix + QStringLiteral("%1 is %2×%3 but needs at least %4×%5")
                                     .arg(describe(widget))
                                     .arg(widget->width())
                                     .arg(widget->height())
                                     .arg(minimumWidth)
                                     .arg(minimumHeight);
    }
    if (primaries.size() > 1)
        problems << prefix + QStringLiteral("shows %1 primary buttons: %2")
                                 .arg(primaries.size())
                                 .arg(primaries.join(QStringLiteral(", ")));
    return problems;
}

namespace {
class DialogLayoutGuard final : public QObject {
  public:
    using QObject::QObject;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() != QEvent::Show && event->type() != QEvent::LayoutRequest &&
            event->type() != QEvent::Resize)
            return false;
        QDialog* dialog = nullptr;
        for (auto* widget = qobject_cast<QWidget*>(watched); widget && !dialog;
             widget = widget->parentWidget())
            dialog = qobject_cast<QDialog*>(widget);
        if (!dialog || pending_.contains(dialog))
            return false;
        pending_.insert(dialog);
        QTimer::singleShot(0, this, [this, dialog, guarded = QPointer<QDialog>(dialog)] {
            pending_.remove(dialog);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            if (guarded && guarded->isVisible() && !qobject_cast<QFileDialog*>(guarded.data()))
                check(*guarded);
        });
        return false;
    }

  private:
    void check(QDialog& dialog) {
        const auto directory = qEnvironmentVariable("CHOSCORDB_DIALOG_CAPTURE_DIR");
        if (!directory.isEmpty()) {
            auto name = (dialog.objectName().isEmpty()
                             ? QString::fromLatin1(dialog.metaObject()->className())
                             : dialog.objectName()) +
                        QLatin1Char('-') + dialog.windowTitle();
            name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")),
                         QStringLiteral("_"));
            dialog.grab().save(QDir(directory).filePath(name + QStringLiteral(".png")));
        }
        for (const auto& problem : dialogLayoutProblems(dialog)) {
            if (reported_.contains(problem))
                continue;
            reported_.insert(problem);
            if (QTest::currentTestFunction())
                QTest::qFail(qPrintable(problem), __FILE__, __LINE__);
            else
                qFatal("%s", qPrintable(problem));
        }
    }

    QSet<QDialog*> pending_;
    QSet<QString> reported_;
};

}
}

static void installDialogLayoutGuard() {
    if (qobject_cast<QApplication*>(QCoreApplication::instance()))
        qApp->installEventFilter(new choscordb::test::DialogLayoutGuard(qApp));
}

Q_COREAPP_STARTUP_FUNCTION(installDialogLayoutGuard)
