#include "app/main_window.h"

#include "app/main_window_ui.h"
#include "app/navigator_controller.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/menu/menu.h"
#include "models/navigator_model.h"
#include <QAction>
#include <QComboBox>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QTimer>
#include <QTreeView>
#include <QVariantMap>
#include <algorithm>

namespace choscordb {
namespace {
QString subtypeFor(const QModelIndex& index) {
    for (const auto& value : index.data(NavigatorModel::PropertiesRole).toList()) {
        const auto property = value.toMap();
        if (property.value(QStringLiteral("name")).toString() == QStringLiteral("Relation subtype"))
            return property.value(QStringLiteral("value")).toString();
    }
    return {};
}
PinRecord recordFor(const QModelIndex& index, const QString& profileId,
                    const QString& profileName) {
    PinRecord pin;
    pin.profileId = profileId;
    pin.profileName = profileName;
    pin.objectId = index.data(NavigatorModel::ObjectIdRole).toString();
    pin.name = index.data(Qt::DisplayRole).toString();
    pin.qualifiedName = index.data(NavigatorModel::QualifiedNameRole).toString();
    pin.kind = index.data(NavigatorModel::KindRole).toString();
    pin.parentObjectId = index.parent().data(NavigatorModel::ObjectIdRole).toString();
    pin.relationSubtype = subtypeFor(index);
    for (auto parent = index.parent(); parent.isValid(); parent = parent.parent())
        if (parent.data(NavigatorModel::KindRole).toString() != QStringLiteral("connection")) {
            pin.ancestryIds.prepend(parent.data(NavigatorModel::ObjectIdRole).toString());
            pin.ancestryNames.prepend(parent.data(Qt::DisplayRole).toString());
        }
    return pin;
}
} // namespace

void MainWindow::initializePins(const Ui& ui) {
    pinnedList_ = ui.pinnedList;
    QString error;
    pins_ = pinStore_.load(&error);
    if (!error.isEmpty())
        showToast(tr("Some saved pins could not be loaded: %1").arg(error), ToastVariant::Warning);
    renderPins();
    navigatorController_->setPinStateResolver(
        [this](const QModelIndex& index) -> std::optional<bool> {
            const auto connection = index.data(NavigatorModel::ConnectionRole).toULongLong();
            const auto profileId = workspace_->profileIdForConnection(connection);
            if (profileId.isEmpty())
                return std::nullopt;
            const auto candidate = recordFor(index, profileId, {});
            if (!PinStore::valid(candidate))
                return std::nullopt;
            const auto key = PinStore::identityKey(candidate);
            for (const auto& pin : pins_)
                if (PinStore::identityKey(pin) == key)
                    return true;
            return false;
        });
    connect(navigatorController_, &NavigatorController::pinRequested, this,
            [this](const QModelIndex& index, bool unpin) {
                if (!index.isValid())
                    return;
                const auto connection = index.data(NavigatorModel::ConnectionRole).toULongLong();
                const auto profileId = workspace_->profileIdForConnection(connection);
                if (profileId.isEmpty()) {
                    showToast(tr("Save this connection before pinning its objects."),
                              ToastVariant::Warning);
                    return;
                }
                QString profileName;
                for (int row = 0; row < savedConnectionsList_->count(); ++row) {
                    const auto* item = savedConnectionsList_->item(row);
                    const auto profile = item->data(Qt::UserRole).value<SavedProfile>();
                    if (profile.id == profileId) {
                        profileName = profile.name;
                        break;
                    }
                }
                const auto candidate = recordFor(index, profileId, profileName);
                if (!PinStore::valid(candidate))
                    return;
                const auto key = PinStore::identityKey(candidate);
                auto updated = pins_;
                for (auto it = updated.begin(); it != updated.end(); ++it)
                    if (PinStore::identityKey(*it) == key) {
                        if (unpin)
                            updated.erase(it);
                        else
                            return;
                        break;
                    }
                if (!unpin)
                    updated.prepend(candidate);
                QString error;
                if (!pinStore_.save(updated, &error)) {
                    showToast(tr("Could not save pins: %1").arg(error), ToastVariant::Danger);
                    return;
                }
                pins_ = std::move(updated);
                renderPins();
            });
    connect(ui.pinnedList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) { activatePin(item->data(Qt::UserRole).toString()); });
    connect(ui.pinnedList, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) { activatePin(item->data(Qt::UserRole).toString()); });
    connect(ui.pinnedList, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        auto* item = pinnedList_->itemAt(point);
        if (!item)
            return;
        const auto key = item->data(Qt::UserRole).toString();
        auto* menu = new QMenu(pinnedList_);
        menu->setObjectName("pinnedMenu");
        menu->setAttribute(Qt::WA_DeleteOnClose);
        auto* unpin = menu->addAction(tr("Unpin"));
        unpin->setObjectName("unpinPinnedObject");
        connect(unpin, &QAction::triggered, this, [this, key] {
            auto updated = pins_;
            for (auto it = updated.begin(); it != updated.end(); ++it)
                if (PinStore::identityKey(*it) == key) {
                    updated.erase(it);
                    QString error;
                    if (!pinStore_.save(updated, &error)) {
                        showToast(tr("Could not save pins: %1").arg(error), ToastVariant::Danger);
                        return;
                    }
                    pins_ = std::move(updated);
                    renderPins();
                    return;
                }
        });
        design::popupContextMenu(*menu, pinnedList_->viewport()->mapToGlobal(point));
    });
    connect(workspace_->adapter(), &EngineAdapter::profilesReady, this,
            [this](quint64 token, const QList<SavedProfile>&) {
                if (token == profileListToken_)
                    renderPins();
            });
    connect(workspace_->adapter(), &EngineAdapter::profileDeleted, this,
            [this](quint64, const QString& profileId, const QString&) {
                auto updated = pins_;
                updated.removeIf([&](const PinRecord& pin) { return pin.profileId == profileId; });
                if (updated.size() == pins_.size())
                    return;
                QString error;
                if (!pinStore_.save(updated, &error)) {
                    showToast(tr("Could not remove deleted profile's pins: %1").arg(error),
                              ToastVariant::Danger);
                    return;
                }
                pins_ = std::move(updated);
                renderPins();
            });
    connect(workspace_, &QueryWorkspace::connectionReady, this, [this](quint64) {
        QTimer::singleShot(0, this, [this] {
            renderPins();
            tryRevealPendingPin();
        });
    });
    connect(savedConnectionsList_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem*) { renderPins(); });
    connect(savedConnectionsList_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem*) { renderPins(); });
    connect(workspace_->adapter(), &EngineAdapter::eventReady, this,
            [this](const BridgeEvent& event) {
                if (event.kind == "connection_failed" || event.kind == "disconnected")
                    renderPins();
                if (!pendingPinKey_.isEmpty() && event.kind == "connection_failed") {
                    const auto pin =
                        std::find_if(pins_.cbegin(), pins_.cend(), [this](const PinRecord& entry) {
                            return PinStore::identityKey(entry) == pendingPinKey_;
                        });
                    if (pin != pins_.cend() && !selectedProfileIds_.contains(pin->profileId) &&
                        !pendingBrowseProfiles_.contains(pin->profileId) &&
                        !selectedSessionIds_.contains(pin->profileId)) {
                        pendingPinKey_.clear();
                        pinStartAttempts_ = 0;
                    }
                }
            });
}

void MainWindow::renderPins() {
    if (!pinnedList_)
        return;
    if (!pendingPinKey_.isEmpty() &&
        std::none_of(pins_.cbegin(), pins_.cend(), [this](const PinRecord& pin) {
            return PinStore::identityKey(pin) == pendingPinKey_;
        }))
        pendingPinKey_.clear();
    pinnedList_->clear();
    auto* connections = findChild<QComboBox*>("connectionSelector");
    for (const auto& pin : pins_) {
        auto profileName = pin.profileName.isEmpty() ? pin.profileId : pin.profileName;
        if (savedConnectionsList_)
            for (int row = 0; row < savedConnectionsList_->count(); ++row) {
                const auto profile =
                    savedConnectionsList_->item(row)->data(Qt::UserRole).value<SavedProfile>();
                if (profile.id == pin.profileId) {
                    profileName = profile.name;
                    break;
                }
            }
        QString context = profileName;
        for (const auto& ancestor : pin.ancestryNames)
            if (!ancestor.isEmpty() && ancestor != profileName)
                context += QStringLiteral(" / ") + ancestor;
        if (!pin.qualifiedName.isEmpty())
            context += QStringLiteral(" · ") + pin.qualifiedName;
        if (!pin.relationSubtype.isEmpty())
            context += QStringLiteral(" · ") + pin.relationSubtype;
        bool liveHidden = false;
        if (connections && !selectedSessionIds_.contains(pin.profileId))
            for (int row = 0; row < connections->count(); ++row)
                if (connections->itemData(row).isValid() &&
                    workspace_->profileIdForConnection(connections->itemData(row).toULongLong()) ==
                        pin.profileId) {
                    liveHidden = true;
                    break;
                }
        const auto status = pin.unavailable                                  ? tr("Unavailable")
                            : pendingBrowseProfiles_.contains(pin.profileId) ? tr("Loading")
                            : selectedSessionIds_.contains(pin.profileId)    ? tr("Connected")
                            : liveHidden                                     ? tr("Hidden")
                                                                             : tr("Disconnected");
        auto* item = new QListWidgetItem(pin.name, pinnedList_);
        item->setData(Qt::UserRole, PinStore::identityKey(pin));
        item->setData(NavigatorModel::KindRole, pin.kind);
        item->setData(Qt::AccessibleDescriptionRole,
                      tr("%1 %2 in %3. %4. Activate to reveal the original object.")
                          .arg(pin.kind, pin.name, context, status));
        item->setToolTip(tr("%1 · %2\n%3 · %4").arg(pin.name, pin.kind, context, status));
    }
}

void MainWindow::activatePin(const QString& key) {
    if (!pendingPinKey_.isEmpty())
        return;
    const auto found = std::find_if(pins_.cbegin(), pins_.cend(), [&](const PinRecord& pin) {
        return PinStore::identityKey(pin) == key;
    });
    if (found == pins_.cend())
        return;
    if (found->unavailable) {
        showToast(tr("This object is unavailable. Unpin it or refresh the original navigator."),
                  ToastVariant::Warning);
        return;
    }
    if (databaseClosePending_ || !workspace_->navigationAllowed() ||
        workspace_->hasPendingEdits() || !allowDocumentChange()) {
        showToast(tr("Finish active database work or pending edits before opening this pin."),
                  ToastVariant::Warning);
        return;
    }
    ++pinActivationGeneration_;
    pinRevealInFlight_ = false;
    pendingPinKey_ = key;
    pinStartAttempts_ = 0;
    if (!reconnectProfile_) {
        pendingPinKey_.clear();
        showToast(tr("The saved connection for this pin was not found."), ToastVariant::Danger);
        return;
    }
    if (!reconnectProfile_(found->profileId)) {
        pendingPinKey_.clear();
        showToast(tr("The saved connection could not be opened. Fix the connection or finish "
                     "active work, then activate the pin to retry."),
                  ToastVariant::Warning);
        return;
    }
    renderPins();
    tryRevealPendingPin();
}

void MainWindow::tryRevealPendingPin() {
    if (pendingPinKey_.isEmpty() || !navigatorController_ || pinRevealInFlight_)
        return;
    const auto found = std::find_if(pins_.cbegin(), pins_.cend(), [this](const PinRecord& pin) {
        return PinStore::identityKey(pin) == pendingPinKey_;
    });
    if (found == pins_.cend()) {
        pendingPinKey_.clear();
        return;
    }
    if (!selectedProfileIds_.contains(found->profileId))
        return;
    if (!selectedSessionIds_.contains(found->profileId))
        return; // Existing saved-profile connection callback resumes the lookup.
    const auto connection = selectedSessionIds_.value(found->profileId);
    const auto key = pendingPinKey_;
    const auto generation = pinActivationGeneration_;
    const auto pin = *found;
    pinRevealInFlight_ = true;
    if (navigatorController_->revealObject(
            connection, pin.ancestryIds, pin.objectId, pin.kind, pin.qualifiedName,
            pin.relationSubtype,
            [this, key, generation](NavigatorController::RevealResult result,
                                    const QString& reason) {
                if (generation != pinActivationGeneration_ || pendingPinKey_ != key)
                    return;
                pinRevealInFlight_ = false;
                pendingPinKey_.clear();
                pinStartAttempts_ = 0;
                if (result == NavigatorController::RevealResult::Found)
                    return;
                if (result == NavigatorController::RevealResult::Unavailable) {
                    auto updated = pins_;
                    for (auto& entry : updated)
                        if (PinStore::identityKey(entry) == key)
                            entry.unavailable = true;
                    QString error;
                    if (pinStore_.save(updated, &error)) {
                        pins_ = std::move(updated);
                        renderPins();
                    } else {
                        showToast(tr("Could not save unavailable pin state: %1").arg(error),
                                  ToastVariant::Danger);
                    }
                }
                showToast(reason, ToastVariant::Warning);
            },
            [this, key, generation] {
                return generation == pinActivationGeneration_ && pendingPinKey_ == key;
            })) {
        pinStartAttempts_ = 0;
        return;
    }
    pinRevealInFlight_ = false;
    if (++pinStartAttempts_ >= 20) {
        pendingPinKey_.clear();
        pinStartAttempts_ = 0;
        showToast(tr("The connection is not ready. Activate the pin to retry."),
                  ToastVariant::Warning);
        return;
    }
    QTimer::singleShot(50, this, [this, key] {
        if (pendingPinKey_ == key)
            tryRevealPendingPin();
    });
}

void MainWindow::updatePinsForObjectAction(const PendingObjectAction& action) {
    const auto profileId = workspace_->profileIdForConnection(action.connection);
    if (profileId.isEmpty())
        return;
    auto updated = pins_;
    bool changed = false;
    for (auto& pin : updated) {
        if (pin.profileId != profileId || pin.objectId != action.objectId ||
            pin.kind != action.kind || pin.qualifiedName != action.qualifiedName)
            continue;
        if (action.action == QStringLiteral("drop"))
            pin.unavailable = true;
        else {
            pin.objectId = action.newObjectId;
            pin.qualifiedName = action.newQualifiedName;
            pin.name = action.newName;
            pin.unavailable = false;
        }
        changed = true;
    }
    if (!changed)
        return;
    QString error;
    if (!pinStore_.save(updated, &error)) {
        showToast(tr("The object changed, but its pin could not be saved: %1").arg(error),
                  ToastVariant::Danger);
        return;
    }
    pins_ = std::move(updated);
    renderPins();
}
} // namespace choscordb
