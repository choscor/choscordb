#include "tools/preview/preview_window.h"

#include "design_system/text/text.h"
#include "design_system/theme_manager.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSysInfo>
#include <QTableView>
#include <QVBoxLayout>

namespace choscordb::design {
void applySpecimenTheme(QWidget& host) {
    if (auto* manager = host.findChild<ThemeManager*>(QString{}, Qt::FindDirectChildrenOnly))
        manager->applyTo(host);
    const auto palette = applicationPalette(resolvedThemeForWidget(host));
    for (auto* scroll : host.findChildren<QScrollArea*>()) {
        if (auto* paper = scroll->widget()) {
            paper->setPalette(palette);
            paper->setBackgroundRole(QPalette::Base);
        }
    }
}
bool PreviewWindow::exportCapture(const QString& path, bool comparison, QSize logicalSize,
                                  ResolvedAppearance appearance) {
    const auto fail = [this](const QString& reason) {
        status_->setText(tr("Capture failed: %1").arg(reason));
        return false;
    };

    if (logicalSize.isEmpty())
        logicalSize = QSize(comparison ? 1280 : 640, 900);
    if (logicalSize.width() < (comparison ? 640 : 320) || logicalSize.width() > 2560 ||
        logicalSize.height() < 320 || logicalSize.height() > 1800 ||
        (comparison && logicalSize.width() % 2 != 0))
        return fail(tr("Invalid capture dimensions."));
    const int paneWidth = logicalSize.width() / (comparison ? 2 : 1);
    const int paneHeight = logicalSize.height();

    // Recreate fixtures so editing, focus/caret blink, scroll position and pointer
    // location in the interactive preview cannot alter a reference capture.
    PreviewWindow fixture;
    (void)fixture.selectSpecimen(specimen_->currentData().toString());
    fixture.resize(1440, 1100);
    fixture.ensurePolished();
    fixture.layout()->activate();
    auto* singleHost = appearance == ResolvedAppearance::Dark ? fixture.dark_ : fixture.light_;
    auto* target = comparison ? fixture.comparison_ : singleHost;
    target->setFixedSize(logicalSize);
    target->ensurePolished();
    target->layout()->activate();
    // Scroll areas settle viewport/scrollbar geometry on show and the queued
    // layout pass. Rendering a hidden fixture immediately can clip the final
    // field edge or active tab indicator beneath stale scrollbars.
    const auto id = specimen_->currentData().toString();
    // These native popups require an active, focusable owner on Cocoa.
    fixture.setAttribute(Qt::WA_DontShowOnScreen, id != "selects");
    fixture.show();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QImage image(logicalSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    target->render(&image);
    QString surface = "inline";
    if (id == "dialogs" || id == "confirmations" || id == "nonmodal" || id == "menus") {
        surface = id == "menus" ? "menu" : id == "nonmodal" ? "nonmodal" : "modal";
        QPainter painter(&image);
        const QList<QWidget*> hosts = comparison ? QList<QWidget*>{fixture.light_, fixture.dark_}
                                                 : QList<QWidget*>{singleHost};
        for (int i = 0; i < hosts.size(); ++i) {
            auto* host = hosts[i];
            auto* previousParent = host->parentWidget();
            const auto previousGeometry = host->geometry();
            host->setParent(nullptr);
            host->setAttribute(Qt::WA_DontShowOnScreen);
            host->setFixedSize(paneWidth, paneHeight);
            // Reparenting can restore Qt's cached base palette. Keep detached
            // popup/backdrop captures scoped to their own Light/Dark specimen.
            applySpecimenTheme(*host);
            host->layout()->activate();
            host->show();
            const auto restoreHost = qScopeGuard([host, previousParent, previousGeometry] {
                host->setParent(previousParent);
                host->setGeometry(previousGeometry);
            });
            auto* trigger = host->findChild<QPushButton*>(id == "menus" ? "previewOpenMenu"
                                                                        : "previewOpenDialog");
            auto* actual =
                trigger
                    ? qobject_cast<QWidget*>(trigger->property("previewSurface").value<QObject*>())
                    : nullptr;
            if (!actual)
                return fail(tr("The selected surface is unavailable."));
            if (surface == "modal")
                actual->setParent(host, actual->windowFlags());
            actual->setAttribute(Qt::WA_DontShowOnScreen);
            actual->ensurePolished();
            if (id == "menus") {
                actual->adjustSize();
            } else {
                actual->adjustSize();
                actual->show();
                if (surface == "modal") {
                    // Use the production backdrop's paint event, including its
                    // token opacity, instead of approximating the overlay here.
                    QWidget* backdrop = nullptr;
                    const auto candidates =
                        actual->parentWidget()->window()->findChildren<QWidget*>(
                            "modalBackdrop", Qt::FindDirectChildrenOnly);
                    for (auto* candidate : candidates) {
                        if (!candidate->isHidden()) {
                            backdrop = candidate;
                            break;
                        }
                    }
                    if (backdrop == nullptr)
                        return fail(tr("The modal backdrop is unavailable."));
                    {
                        backdrop->resize(paneWidth, paneHeight);
                        backdrop->render(&painter, QPoint(i * paneWidth, 0), {},
                                         QWidget::DrawChildren);
                    }
                }
            }
            if (actual->layout())
                actual->layout()->activate();
            if (id == "menus") {
                auto* menu = qobject_cast<QMenu*>(actual);
                menu->popup(QPoint(0, 0));
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                const auto snapshot = menu->grab();
                if (snapshot.isNull())
                    return fail(tr("The real menu could not be captured."));
                painter.drawPixmap(QPoint(i * paneWidth + (paneWidth - menu->width()) / 2,
                                          (paneHeight - menu->height()) / 2),
                                   snapshot);
            } else {
                actual->render(&painter, QPoint(i * paneWidth + (paneWidth - actual->width()) / 2,
                                                (paneHeight - actual->height()) / 2));
            }
            actual->hide();
        }
    }

    if (id == "selects") {
        surface = "selector-popup";
        fixture.show();
        fixture.activateWindow();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        const QList<QWidget*> hosts = comparison ? QList<QWidget*>{fixture.light_, fixture.dark_}
                                                 : QList<QWidget*>{singleHost};
        QPainter painter(&image);
        for (int i = 0; i < hosts.size(); ++i) {
            auto* select = hosts[i]->findChild<QComboBox*>();
            select->showPopup();
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            auto* popup = select->view()->parentWidget();
            if (!popup->isVisible())
                return fail(tr("The real selector popup did not become ready."));
            painter.drawPixmap(QPoint(i * paneWidth + (paneWidth - popup->width()) / 2,
                                      (paneHeight - popup->height()) / 2),
                               popup->grab());
            select->hidePopup();
        }
    }
    if (id == "tooltip-popover") {
        surface = "tooltip";
        const QList<QWidget*> hosts = comparison ? QList<QWidget*>{fixture.light_, fixture.dark_}
                                                 : QList<QWidget*>{singleHost};
        QPainter painter(&image);
        for (int i = 0; i < hosts.size(); ++i) {
            hosts[i]->findChild<QPushButton*>("previewOpenTooltip")->click();
            auto* tooltip = fixture.findChild<QWidget*>("designTooltip");
            if (!tooltip || !tooltip->isVisible()) {
                return fail(tr("The real tooltip did not become ready."));
            }
            tooltip->render(&painter, QPoint(i * paneWidth + (paneWidth - tooltip->width()) / 2,
                                             (paneHeight - tooltip->height()) / 2));
            tooltip->hide();
        }
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        return fail(output.errorString());
    }
    if (!image.save(&output, "PNG") || !output.commit()) {
        return fail(output.errorString());
    }
    QJsonObject metadata{
        {"section", navigation_->currentItem()->text()},
        {"specimen", specimen_->currentData().toString()},
        {"source", source_->text()},
        {"surface", surface},
        {"logicalWidth", logicalSize.width()},
        {"logicalHeight", logicalSize.height()},
        {"scale", 1},
        {"sourceDeviceScale", target->devicePixelRatioF()},
        {"rendering", "QWidget logical-pixel render; native popup content; excludes OS shell"},
        {"themes", comparison                               ? "Light / Dark"
                   : appearance == ResolvedAppearance::Dark ? "Dark"
                                                            : "Light"},
        {"font", resolveTypography(TypographyRole::Ui).family()},
        {"qt", QT_VERSION_STR},
        {"platform", QGuiApplication::platformName()},
        {"os", QSysInfo::prettyProductName()},
        {"fixture", surface == "inline"
                        ? "synthetic; initial state; no focus; reduced motion"
                        : "synthetic; open real surface; no action dispatched; reduced motion"}};
    QJsonArray controls;
    for (auto* host : (comparison ? QList<QWidget*>{fixture.light_, fixture.dark_}
                                  : QList<QWidget*>{singleHost})) {
        for (auto* control : host->findChildren<QWidget*>()) {
            if (control->objectName().isEmpty() || control->isWindow())
                continue;
            const auto point = control->mapTo(target, QPoint());
            controls.append(QJsonObject{{"name", control->objectName()},
                                        {"theme", host == fixture.dark_ ? "Dark" : "Light"},
                                        {"x", point.x()},
                                        {"y", point.y()},
                                        {"width", control->width()},
                                        {"height", control->height()}});
        }
    }
    metadata.insert("controls", controls);
    QSaveFile manifest(path + ".json");
    const auto bytes = QJsonDocument(metadata).toJson();
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write(bytes) != bytes.size() ||
        !manifest.commit()) {
        return fail(
            tr("PNG written, but metadata could not be saved: %1").arg(manifest.errorString()));
    }
    status_->setText(tr("Capture saved: %1").arg(path));
    return true;
}
} // namespace choscordb::design
