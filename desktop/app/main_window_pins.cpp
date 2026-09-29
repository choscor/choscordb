#include "app/main_window.h"

#include "app/main_window_ui.h"
#include "app/navigator_controller.h"
#include "app/pinned_tree_model.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/menu/menu.h"
#include "design_system/theme_manager.h"
#include "models/navigator_model.h"
#include <QAbstractItemModel>
#include <QAction>
#include <QComboBox>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeView>
#include <QVariantMap>
#include <algorithm>
#include <utility>

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
    pinnedModel_ = new PinnedTreeModel(navigatorController_->model(), pinnedList_);
    pinnedList_->setModel(pinnedModel_);
    const auto updatePinnedGeometry = [this] {
        if (!pinnedList_ || !pinnedModel_)
            return;
        const bool hasPins = pinnedModel_->rowCount() > 0;
        auto* section = findChild<QWidget*>("pinnedSection");
        if (section)
            section->setVisible(hasPins);
        pinnedList_->setVisible(hasPins);
        if (!hasPins)
            return;
        const auto countVisible = [this](auto&& self, const QModelIndex& parent) -> int {
            int count = 0;
            for (int row = 0; row < pinnedModel_->rowCount(parent); ++row) {
                const auto child = pinnedModel_->index(row, 0, parent);
                ++count;
                if (pinnedList_->isExpanded(child))
                    count += self(self, child);
            }
            return count;
        };
        const int rowHeight =
            std::max(theme_->metrics().navigationRowHeight, pinnedList_->sizeHintForRow(0));
        pinnedList_->setFixedHeight(countVisible(countVisible, {}) * rowHeight +
                                    2 * pinnedList_->frameWidth());
    };
    const auto schedulePinnedGeometry = [this, updatePinnedGeometry] {
        QTimer::singleShot(0, pinnedList_, updatePinnedGeometry);
    };
    connect(pinnedModel_, &QAbstractItemModel::rowsInserted, pinnedList_, schedulePinnedGeometry);
    connect(pinnedModel_, &QAbstractItemModel::rowsRemoved, pinnedList_, schedulePinnedGeometry);
    connect(pinnedModel_, &QAbstractItemModel::modelReset, pinnedList_, schedulePinnedGeometry);
    connect(pinnedModel_, &QAbstractItemModel::dataChanged, pinnedList_, schedulePinnedGeometry);
    connect(pinnedModel_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex&, const QModelIndex&) {
                QTimer::singleShot(0, this, [this] {
                    for (int row = 0; row < pinnedModel_->rowCount(); ++row) {
                        const auto root = pinnedModel_->index(row, 0);
                        const auto key = pinnedModel_->pinKey(root);
                        if (!pinnedList_->isExpanded(root) ||
                            pinnedModel_->sourceIndex(root).isValid() ||
                            pendingExpansionKeys_.contains(key) ||
                            pinExpansionErrors_.contains(key))
                            continue;
                        const auto pin =
                            std::find_if(pins_.cbegin(), pins_.cend(), [&](const PinRecord& entry) {
                                return PinStore::identityKey(entry) == key;
                            });
                        if (pin == pins_.cend() || pin->unavailable ||
                            !selectedProfileIds_.contains(pin->profileId) ||
                            !selectedSessionIds_.contains(pin->profileId))
                            continue;
                        pendingExpansionKeys_.insert(key);
                        pinExpansionRebinding_.insert(key);
                        pinExpansionConnectionIds_.insert(
                            key, selectedSessionIds_.value(pin->profileId));
                        ++pinExpansionGenerations_[key];
                        pinnedModel_->setStatus(key, tr("Loading children…"));
                    }
                    tryExpandPendingPins();
                });
            });
    connect(pinnedList_, &QTreeView::expanded, this,
            [this, updatePinnedGeometry](const QModelIndex& index) {
                updatePinnedGeometry();
                if (pinnedModel_->isPinnedRoot(index))
                    expandPin(pinnedModel_->pinKey(index));
            });
    connect(pinnedList_, &QTreeView::collapsed, this,
            [this, updatePinnedGeometry](const QModelIndex& index) {
                updatePinnedGeometry();
                if (!pinnedModel_->isPinnedRoot(index))
                    return;
                const auto key = pinnedModel_->pinKey(index);
                pendingExpansionKeys_.remove(key);
                pinExpansionInFlight_.remove(key);
                pinExpansionRebinding_.remove(key);
                pinExpansionErrors_.remove(key);
                pinExpansionConnectionIds_.remove(key);
                ++pinExpansionGenerations_[key];
                pinnedModel_->setResolved(key, {});
                renderPins();
            });
    connect(theme_, &design::ThemeManager::metricsChanged, pinnedList_, schedulePinnedGeometry);
    schedulePinnedGeometry();
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
    connect(ui.pinnedList, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        if (pinnedModel_->isPinnedRoot(index))
            activatePin(pinnedModel_->pinKey(index));
    });
    connect(ui.pinnedList, &QTreeView::activated, this, [this](const QModelIndex& index) {
        if (pinnedModel_->isPinnedRoot(index))
            activatePin(pinnedModel_->pinKey(index));
        else {
            const auto source = pinnedModel_->sourceIndex(index);
            if (source.data(NavigatorModel::KindRole).toString() == QStringLiteral("load_more"))
                navigatorController_->model()->requestNextPage(source);
        }
    });
    connect(ui.pinnedList->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex& previous) {
                if (pinnedModel_->isPinnedRoot(current) || !current.isValid())
                    return;
                if (!allowDocumentChange()) {
                    const QSignalBlocker blocker(pinnedList_->selectionModel());
                    pinnedList_->selectionModel()->setCurrentIndex(
                        previous, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                    return;
                }
                activateNavigatorObject(pinnedModel_->sourceIndex(current));
            });
    connect(ui.pinnedList, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        auto index = pinnedList_->indexAt(point);
        auto menuPoint = point;
        if (!index.isValid() && point.x() < 0 && point.y() < 0) {
            index = pinnedList_->currentIndex();
            menuPoint = pinnedList_->visualRect(index).center();
        }
        if (!index.isValid())
            return;
        if (!pinnedModel_->isPinnedRoot(index)) {
            QMenu menu(pinnedList_);
            navigatorController_->populateContextMenu(&menu, pinnedModel_->sourceIndex(index));
            design::execContextMenu(menu, pinnedList_->viewport()->mapToGlobal(menuPoint));
            return;
        }
        const auto key = pinnedModel_->pinKey(index);
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
        design::popupContextMenu(*menu, pinnedList_->viewport()->mapToGlobal(menuPoint));
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
            tryExpandPendingPins();
        });
    });
    connect(savedConnectionsList_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem*) { renderPins(); });
    connect(savedConnectionsList_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem*) { renderPins(); });
    connect(workspace_->adapter(), &EngineAdapter::eventReady, this,
            [this](const BridgeEvent& event) {
                if (event.kind == "connection_failed") {
                    for (const auto& key : pendingExpansionKeys_.values()) {
                        if (!pinExpansionConnectionIds_.contains(key) ||
                            pinExpansionConnectionIds_.value(key) != event.id)
                            continue;
                        const auto reason = tr("Connection failed. Collapse and expand to retry.");
                        pinExpansionErrors_.insert(key, reason);
                        pinnedModel_->setStatus(key, reason);
                        pendingExpansionKeys_.remove(key);
                        pinExpansionInFlight_.remove(key);
                        pinExpansionRebinding_.remove(key);
                    }
                }
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
    if (!pinnedModel_)
        return;
    for (auto it = pinExpansionErrors_.begin(); it != pinExpansionErrors_.end();) {
        const auto found = std::find_if(pins_.cbegin(), pins_.cend(), [&](const PinRecord& pin) {
            return PinStore::identityKey(pin) == it.key();
        });
        it = found == pins_.cend() ? pinExpansionErrors_.erase(it) : ++it;
    }
    if (!pendingPinKey_.isEmpty() &&
        std::none_of(pins_.cbegin(), pins_.cend(), [this](const PinRecord& pin) {
            return PinStore::identityKey(pin) == pendingPinKey_;
        }))
        pendingPinKey_.clear();
    for (const auto& key : pendingExpansionKeys_.values()) {
        const auto found = std::find_if(pins_.cbegin(), pins_.cend(), [&](const PinRecord& pin) {
            return PinStore::identityKey(pin) == key;
        });
        if (found != pins_.cend() && (selectedProfileIds_.contains(found->profileId) ||
                                      pendingBrowseProfiles_.contains(found->profileId)))
            continue;
        pendingExpansionKeys_.remove(key);
        pinExpansionInFlight_.remove(key);
        pinExpansionRebinding_.remove(key);
        pinExpansionConnectionIds_.remove(key);
        ++pinExpansionGenerations_[key];
        pinExpansionErrors_.insert(
            key, tr("Connection no longer selected. Collapse and expand to retry."));
    }
    QList<PinRecord> displayPins;
    auto* connections = findChild<QComboBox*>("connectionSelector");
    for (auto pin : pins_) {
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
        pin.profileName = profileName;
        displayPins.append(pin);
    }
    pinnedModel_->setPins(displayPins);
    for (const auto& pin : displayPins) {
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
        const auto key = PinStore::identityKey(pin);
        if (!pendingExpansionKeys_.contains(key))
            pinnedModel_->setStatus(key, pinExpansionErrors_.value(key, status));
        if (!selectedSessionIds_.contains(pin.profileId))
            pinnedModel_->setResolved(key, {});
    }
}

void MainWindow::expandPin(const QString& key) {
    const auto found = std::find_if(pins_.cbegin(), pins_.cend(), [&](const PinRecord& pin) {
        return PinStore::identityKey(pin) == key;
    });
    if (found == pins_.cend() || found->unavailable)
        return;
    pendingExpansionKeys_.insert(key);
    pinExpansionErrors_.remove(key);
    pinExpansionConnectionIds_.remove(key);
    pinExpansionRebinding_.remove(key);
    ++pinExpansionGenerations_[key];
    pinExpansionAttempts_.remove(key);
    pinnedModel_->setResolved(key, {});
    pinnedModel_->setStatus(key, tr("Loading children…"));
    if (databaseClosePending_ || !workspace_->navigationAllowed() ||
        workspace_->hasPendingEdits() || !reconnectProfile_ ||
        !reconnectProfile_(found->profileId)) {
        pendingExpansionKeys_.remove(key);
        const auto reason = tr("Connection unavailable. Collapse and expand to retry.");
        pinExpansionErrors_.insert(key, reason);
        pinnedModel_->setStatus(key, reason);
        showToast(reason, ToastVariant::Warning);
        return;
    }
    if (pendingBrowseProfiles_.contains(found->profileId) &&
        pendingBrowseProfiles_.value(found->profileId).connection)
        pinExpansionConnectionIds_.insert(
            key, *pendingBrowseProfiles_.value(found->profileId).connection);
    else if (selectedSessionIds_.contains(found->profileId))
        pinExpansionConnectionIds_.insert(key, selectedSessionIds_.value(found->profileId));
    tryExpandPendingPins();
}

void MainWindow::tryExpandPendingPins() {
    if (!pinnedModel_ || !navigatorController_)
        return;
    const auto keys = pendingExpansionKeys_.values();
    for (const auto& key : keys) {
        if (pinExpansionInFlight_.contains(key))
            continue;
        const auto found = std::find_if(pins_.cbegin(), pins_.cend(), [&](const PinRecord& pin) {
            return PinStore::identityKey(pin) == key;
        });
        if (found == pins_.cend()) {
            pendingExpansionKeys_.remove(key);
            continue;
        }
        if (!selectedSessionIds_.contains(found->profileId))
            continue;
        const auto connection = selectedSessionIds_.value(found->profileId);
        const auto generation = pinExpansionGenerations_.value(key);
        const auto pin = *found;
        pinExpansionInFlight_.insert(key);
        if (navigatorController_->resolveObject(
                connection, pin.ancestryIds, pin.objectId, pin.kind, pin.qualifiedName,
                pin.relationSubtype,
                [this, key, generation, profileId = pin.profileId,
                 connection](NavigatorController::RevealResult result, const QString& reason,
                             const QModelIndex& source) {
                    if (generation != pinExpansionGenerations_.value(key) ||
                        !pendingExpansionKeys_.contains(key) ||
                        !selectedProfileIds_.contains(profileId) ||
                        selectedSessionIds_.value(profileId) != connection)
                        return;
                    pinExpansionInFlight_.remove(key);
                    pendingExpansionKeys_.remove(key);
                    pinExpansionRebinding_.remove(key);
                    pinExpansionAttempts_.remove(key);
                    if (result == NavigatorController::RevealResult::Found &&
                        pinnedModel_->setResolved(key, source)) {
                        pinnedModel_->setStatus(key, tr("Connected"));
                        for (int row = 0; row < pinnedModel_->rowCount(); ++row) {
                            const auto root = pinnedModel_->index(row, 0);
                            if (pinnedModel_->pinKey(root) == key &&
                                pinnedModel_->canFetchMore(root))
                                pinnedModel_->fetchMore(root);
                        }
                        return;
                    }
                    if (result == NavigatorController::RevealResult::Unavailable) {
                        auto updated = pins_;
                        for (auto& entry : updated)
                            if (PinStore::identityKey(entry) == key)
                                entry.unavailable = true;
                        QString error;
                        if (pinStore_.save(updated, &error)) {
                            pins_ = std::move(updated);
                            renderPins();
                        } else
                            showToast(tr("Could not save unavailable pin state: %1").arg(error),
                                      ToastVariant::Danger);
                    }
                    auto visibleReason = reason;
                    visibleReason.replace(tr("Activate the pin to retry."),
                                          tr("Collapse and expand to retry."));
                    if (visibleReason.isEmpty())
                        visibleReason =
                            tr("Could not load children. Collapse and expand to retry.");
                    pinExpansionErrors_.insert(key, visibleReason);
                    pinnedModel_->setStatus(key, visibleReason);
                    showToast(reason, ToastVariant::Warning);
                },
                [this, key, generation, profileId = pin.profileId, connection] {
                    return generation == pinExpansionGenerations_.value(key) &&
                           pendingExpansionKeys_.contains(key) &&
                           selectedProfileIds_.contains(profileId) &&
                           selectedSessionIds_.value(profileId) == connection;
                },
                !pinExpansionRebinding_.contains(key)))
            continue;
        pinExpansionInFlight_.remove(key);
        if (++pinExpansionAttempts_[key] >= 20) {
            pendingExpansionKeys_.remove(key);
            const auto reason = tr("Connection not ready. Collapse and expand to retry.");
            pinExpansionErrors_.insert(key, reason);
            pinnedModel_->setStatus(key, reason);
            continue;
        }
        QTimer::singleShot(50, this, [this, key, generation] {
            if (pendingExpansionKeys_.contains(key) &&
                pinExpansionGenerations_.value(key) == generation)
                tryExpandPendingPins();
        });
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
            [this, key, generation, pin, connection](NavigatorController::RevealResult result,
                                                     const QString& reason) {
                if (generation != pinActivationGeneration_ || pendingPinKey_ != key)
                    return;
                pinRevealInFlight_ = false;
                pendingPinKey_.clear();
                pinStartAttempts_ = 0;
                if (result == NavigatorController::RevealResult::Found) {
                    const bool tableDetail = pin.kind == QLatin1String("index") ||
                                             pin.kind == QLatin1String("primarykey") ||
                                             pin.kind == QLatin1String("foreignkey") ||
                                             pin.kind == QLatin1String("uniquekey");
                    auto* tree = findChild<QTreeView*>("databaseNavigator");
                    const auto selected = tree ? tree->currentIndex() : QModelIndex{};
                    const auto selectedKind = selected.data(NavigatorModel::KindRole).toString();
                    if (tableDetail &&
                        (selectedKind == QLatin1String("table") ||
                         selectedKind == QLatin1String("view")) &&
                        selected.data(NavigatorModel::ObjectIdRole).toString() ==
                            pin.parentObjectId &&
                        !pin.ancestryIds.isEmpty() &&
                        pin.ancestryIds.last() == pin.parentObjectId) {
                        const int pane = pin.kind == QLatin1String("index") ? 1 : 2;
                        openObjectTab(connection, pin.parentObjectId,
                                      selected.data(NavigatorModel::QualifiedNameRole).toString(),
                                      selectedKind,
                                      selected.data(NavigatorModel::PropertiesRole).toList(), pane);
                    }
                    return;
                }
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
