#include "app/main_window.h"

#include "app/main_window_ui.h"
#include "app/main_window_widgets.h"
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
#include <QFutureWatcher>
#include <QHash>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeView>
#include <QVariantMap>
#include <QtConcurrentRun>
#include <algorithm>
#include <iterator>
#include <utility>

namespace choscordb {
namespace {
void showPinActivationStatus(MainWindow* window, const QString& key, const QString& message,
                             ToastVariant variant) {
    window->setProperty("pinActivationNoticeKey", key);
    window->setProperty("pinActivationNoticeMessage", message);
    window->showStatus(message, variant, QStringLiteral("pins"));
}
void clearPinActivationStatus(MainWindow* window, const QString& key) {
    if (window->property("pinActivationNoticeKey").toString() != key)
        return;
    const auto message = window->property("pinActivationNoticeMessage").toString();
    if (!message.isEmpty())
        window->clearStatus(QStringLiteral("pins"), message);
    window->setProperty("pinActivationNoticeKey", QVariant{});
    window->setProperty("pinActivationNoticeMessage", QVariant{});
}
QString subtypeFor(const QModelIndex& index) {
    return NavigatorModel::relationSubtype(index.data(NavigatorModel::PropertiesRole).toList());
}
// Identity keys cross the Rust bridge; compute them once per pin list.
QHash<QString, qsizetype> pinRowsFor(const QList<PinRecord>& pins) {
    QHash<QString, qsizetype> rows;
    rows.reserve(pins.size());
    for (qsizetype row = 0; row < pins.size(); ++row) {
        const auto key = PinStore::identityKey(pins[row]);
        if (!rows.contains(key))
            rows.insert(key, row);
    }
    return rows;
}
const PinRecord* pinFor(const QList<PinRecord>& pins, const QHash<QString, qsizetype>& rows,
                        const QString& key) {
    const auto row = rows.value(key, -1);
    return row >= 0 && row < pins.size() ? &pins[row] : nullptr;
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
        if (!EngineAdapter::objectKindTraits(parent.data(NavigatorModel::KindRole).toString())
                 .connection) {
            pin.ancestryIds.prepend(parent.data(NavigatorModel::ObjectIdRole).toString());
            pin.ancestryNames.prepend(parent.data(Qt::DisplayRole).toString());
        }
    return pin;
}
} // namespace

void MainWindow::initializePins(const Ui& ui) {
    pinIoPool_.setMaxThreadCount(1);
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
        // Rows below a collapsed branch are not shown; expanding it reruns this pass.
        const auto updateRows = [this](auto&& self, const QModelIndex& parent) -> void {
            for (int row = 0; row < pinnedModel_->rowCount(parent); ++row) {
                const auto child = pinnedModel_->index(row, 0, parent);
                const bool hidden = !showsSidebarChild(child);
                if (pinnedList_->isRowHidden(row, parent) != hidden)
                    pinnedList_->setRowHidden(row, parent, hidden);
                if (!hidden && pinnedList_->isExpanded(child))
                    self(self, child);
            }
        };
        updateRows(updateRows, {});
        const auto visibleHeight = [this](auto&& self, const QModelIndex& parent) -> int {
            int height = 0;
            for (int row = 0; row < pinnedModel_->rowCount(parent); ++row) {
                if (pinnedList_->isRowHidden(row, parent))
                    continue;
                const auto child = pinnedModel_->index(row, 0, parent);
                height += pinnedList_->sizeHintForIndex(child).height();
                if (pinnedList_->isExpanded(child))
                    height += self(self, child);
            }
            return height;
        };
        pinnedList_->setFixedHeight(visibleHeight(visibleHeight, {}) +
                                    2 * pinnedList_->frameWidth());
    };
    auto* pinnedGeometryTimer = new QTimer(pinnedList_);
    pinnedGeometryTimer->setSingleShot(true);
    pinnedGeometryTimer->setInterval(0);
    connect(pinnedGeometryTimer, &QTimer::timeout, pinnedList_, updatePinnedGeometry);
    const auto schedulePinnedGeometry = [pinnedGeometryTimer] {
        if (!pinnedGeometryTimer->isActive())
            pinnedGeometryTimer->start();
    };
    new main_window_detail::SidebarWidthObserver(pinnedList_->viewport(), schedulePinnedGeometry);
    connect(pinnedModel_, &QAbstractItemModel::rowsInserted, pinnedList_, schedulePinnedGeometry);
    connect(pinnedModel_, &QAbstractItemModel::rowsRemoved, pinnedList_, schedulePinnedGeometry);
    connect(pinnedModel_, &QAbstractItemModel::modelReset, pinnedList_, schedulePinnedGeometry);
    connect(pinnedModel_, &QAbstractItemModel::dataChanged, pinnedList_, schedulePinnedGeometry);
    // Rebind expanded pins whose source went away, once per burst of row updates.
    auto* pinRebindTimer = new QTimer(this);
    pinRebindTimer->setSingleShot(true);
    pinRebindTimer->setInterval(0);
    connect(pinnedModel_, &QAbstractItemModel::dataChanged, pinRebindTimer,
            [pinRebindTimer](const QModelIndex&, const QModelIndex&) {
                if (!pinRebindTimer->isActive())
                    pinRebindTimer->start();
            });
    connect(pinRebindTimer, &QTimer::timeout, this, [this] {
        {
            for (int row = 0; row < pinnedModel_->rowCount(); ++row) {
                const auto root = pinnedModel_->index(row, 0);
                const auto key = pinnedModel_->pinKey(root);
                if (!pinnedList_->isExpanded(root) || pinnedModel_->sourceIndex(root).isValid() ||
                    pendingExpansionKeys_.contains(key) || pinExpansionErrors_.contains(key))
                    continue;
                const auto* pin = pinFor(pins_, pinRows_, key);
                if (!pin || pin->unavailable || !selectedProfileIds_.contains(pin->profileId) ||
                    !selectedSessionIds_.contains(pin->profileId))
                    continue;
                pendingExpansionKeys_.insert(key);
                pinExpansionRebinding_.insert(key);
                pinExpansionConnectionIds_.insert(key, selectedSessionIds_.value(pin->profileId));
                ++pinExpansionGenerations_[key];
                pinnedModel_->setStatus(key, tr("Loading children…"),
                                        PinnedTreeModel::StatusPlacement::Label);
            }
            tryExpandPendingPins();
        }
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
    schedulePinnedGeometry();
    auto* loadWatcher = new QFutureWatcher<std::pair<QList<PinRecord>, QString>>(this);
    connect(loadWatcher, &QFutureWatcher<std::pair<QList<PinRecord>, QString>>::finished, this,
            [this, loadWatcher, updatePinnedGeometry] {
                const auto [loaded, error] = loadWatcher->result();
                loadWatcher->deleteLater();
                pinsLoaded_ = true;
                if (pinSaveGeneration_ == 0) {
                    pins_ = loaded;
                    pinRows_ = pinRowsFor(pins_);
                    savedPins_ = loaded;
                    renderPins();
                    updatePinnedGeometry();
                }
                if (!error.isEmpty())
                    showStatus(tr("Some saved pins could not be loaded: %1").arg(error),
                               ToastVariant::Warning, QStringLiteral("pins"));
            });
    loadWatcher->setFuture(QtConcurrent::run(&pinIoPool_, [store = pinStore_] {
        QString error;
        auto loaded = store.load(&error);
        return std::make_pair(std::move(loaded), std::move(error));
    }));
    navigatorController_->setPinStateResolver(
        [this](const QModelIndex& index) -> std::optional<bool> {
            const auto connection = index.data(NavigatorModel::ConnectionRole).toULongLong();
            const auto profileId = workspace_->profileIdForConnection(connection);
            if (profileId.isEmpty())
                return std::nullopt;
            const auto candidate = recordFor(index, profileId, {});
            if (!PinStore::valid(candidate))
                return std::nullopt;
            return pinRows_.contains(PinStore::identityKey(candidate));
        });
    connect(navigatorController_, &NavigatorController::pinRequested, this,
            [this](const QModelIndex& index, bool unpin) {
                if (!pinsLoaded_)
                    return;
                if (!index.isValid())
                    return;
                const auto connection = index.data(NavigatorModel::ConnectionRole).toULongLong();
                const auto profileId = workspace_->profileIdForConnection(connection);
                if (profileId.isEmpty()) {
                    showStatus(tr("Save this connection before pinning its objects."),
                               ToastVariant::Warning, QStringLiteral("pins"));
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
                auto updated =
                    PinStore::toggled(pins_, recordFor(index, profileId, profileName), unpin);
                if (updated)
                    savePinsAsync(std::move(*updated), tr("Could not save pins: %1"));
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
            if (auto updated = PinStore::withoutPin(pins_, key))
                savePinsAsync(std::move(*updated), tr("Could not save pins: %1"));
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
                if (auto updated = PinStore::withoutProfile(pins_, profileId))
                    savePinsAsync(std::move(*updated),
                                  tr("Could not remove deleted profile's pins: %1"));
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
    connect(
        workspace_->adapter(), &EngineAdapter::eventReady, this, [this](const BridgeEvent& event) {
            if (event.kind == "connection_failed") {
                for (const auto& key : pendingExpansionKeys_.values()) {
                    if (!pinExpansionConnectionIds_.contains(key) ||
                        pinExpansionConnectionIds_.value(key) != event.id)
                        continue;
                    const auto reason = tr("Connection failed. Collapse and expand to retry.");
                    pinExpansionErrors_.insert(key, reason);
                    pinnedModel_->setStatus(key, reason, PinnedTreeModel::StatusPlacement::Label);
                    pendingExpansionKeys_.remove(key);
                    pinExpansionInFlight_.remove(key);
                    pinExpansionRebinding_.remove(key);
                }
            }
            if (event.kind == "connection_failed" || event.kind == "disconnected")
                renderPins();
            if (!pendingPinKey_.isEmpty() && event.kind == "connection_failed") {
                const auto* pin = pinFor(pins_, pinRows_, pendingPinKey_);
                if (pin && !selectedProfileIds_.contains(pin->profileId) &&
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
    for (auto it = pinExpansionErrors_.begin(); it != pinExpansionErrors_.end();)
        it = pinRows_.contains(it.key()) ? std::next(it) : pinExpansionErrors_.erase(it);
    if (!pendingPinKey_.isEmpty() && !pinRows_.contains(pendingPinKey_))
        pendingPinKey_.clear();
    for (const auto& key : pendingExpansionKeys_.values()) {
        const auto* found = pinFor(pins_, pinRows_, key);
        if (found && (selectedProfileIds_.contains(found->profileId) ||
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
    QHash<QString, QString> profileNames;
    if (savedConnectionsList_)
        for (int row = 0; row < savedConnectionsList_->count(); ++row) {
            const auto profile =
                savedConnectionsList_->item(row)->data(Qt::UserRole).value<SavedProfile>();
            if (!profileNames.contains(profile.id))
                profileNames.insert(profile.id, profile.name);
        }
    QSet<QString> liveProfiles;
    auto* connections = findChild<QComboBox*>("connectionSelector");
    if (connections)
        for (int row = 0; row < connections->count(); ++row)
            if (connections->itemData(row).isValid())
                liveProfiles.insert(
                    workspace_->profileIdForConnection(connections->itemData(row).toULongLong()));
    QList<PinRecord> displayPins;
    displayPins.reserve(pins_.size());
    for (auto pin : pins_) {
        pin.profileName = profileNames.value(
            pin.profileId, pin.profileName.isEmpty() ? pin.profileId : pin.profileName);
        displayPins.append(pin);
    }
    pinnedModel_->setPins(displayPins);
    for (qsizetype row = 0; row < displayPins.size(); ++row) {
        const auto& pin = displayPins[row];
        const bool liveHidden = connections && !selectedSessionIds_.contains(pin.profileId) &&
                                liveProfiles.contains(pin.profileId);
        const bool loading = pendingBrowseProfiles_.contains(pin.profileId);
        const auto status = pin.unavailable                               ? tr("Unavailable")
                            : loading                                     ? tr("Loading")
                            : selectedSessionIds_.contains(pin.profileId) ? tr("Connected")
                            : liveHidden                                  ? tr("Hidden")
                                                                          : tr("Disconnected");
        // setPins() keyed each root row in displayPins order.
        const auto key = pinnedModel_->pinKey(pinnedModel_->index(static_cast<int>(row), 0));
        if (!pendingExpansionKeys_.contains(key)) {
            const auto error = pinExpansionErrors_.constFind(key);
            const bool notice = error != pinExpansionErrors_.cend() || loading;
            pinnedModel_->setStatus(key,
                                    error != pinExpansionErrors_.cend() ? error.value() : status,
                                    notice ? PinnedTreeModel::StatusPlacement::Label
                                           : PinnedTreeModel::StatusPlacement::TooltipOnly);
        }
        if (!selectedSessionIds_.contains(pin.profileId))
            pinnedModel_->setResolved(key, {});
    }
}

void MainWindow::expandPin(const QString& key) {
    const auto* found = pinFor(pins_, pinRows_, key);
    if (!found || found->unavailable)
        return;
    if (pinExpansionErrors_.contains(key))
        clearStatus(QStringLiteral("pins"), pinExpansionErrors_.value(key));
    pendingExpansionKeys_.insert(key);
    pinExpansionErrors_.remove(key);
    pinExpansionConnectionIds_.remove(key);
    pinExpansionRebinding_.remove(key);
    ++pinExpansionGenerations_[key];
    pinExpansionAttempts_.remove(key);
    pinnedModel_->setResolved(key, {});
    pinnedModel_->setStatus(key, tr("Loading children…"), PinnedTreeModel::StatusPlacement::Label);
    if (databaseClosePending_ || !workspace_->navigationAllowed() ||
        workspace_->hasPendingEdits() || !reconnectProfile_ ||
        !reconnectProfile_(found->profileId)) {
        pendingExpansionKeys_.remove(key);
        const auto reason = tr("Connection unavailable. Collapse and expand to retry.");
        pinExpansionErrors_.insert(key, reason);
        pinnedModel_->setStatus(key, reason, PinnedTreeModel::StatusPlacement::Label);
        showStatus(reason, ToastVariant::Warning, QStringLiteral("pins"));
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
        const auto* found = pinFor(pins_, pinRows_, key);
        if (!found) {
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
                        pinnedModel_->setStatus(key, tr("Connected"),
                                                PinnedTreeModel::StatusPlacement::TooltipOnly);
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
                        savePinsAsync(std::move(updated),
                                      tr("Could not save unavailable pin state: %1"));
                    }
                    auto visibleReason = reason;
                    visibleReason.replace(tr("Activate the pin to retry."),
                                          tr("Collapse and expand to retry."));
                    if (visibleReason.isEmpty())
                        visibleReason =
                            tr("Could not load children. Collapse and expand to retry.");
                    pinExpansionErrors_.insert(key, visibleReason);
                    pinnedModel_->setStatus(key, visibleReason,
                                            PinnedTreeModel::StatusPlacement::Label);
                    showStatus(visibleReason, ToastVariant::Warning, QStringLiteral("pins"));
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
            pinnedModel_->setStatus(key, reason, PinnedTreeModel::StatusPlacement::Label);
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
    const auto* found = pinFor(pins_, pinRows_, key);
    if (!found)
        return;
    if (found->unavailable) {
        showPinActivationStatus(
            this, key,
            tr("This object is unavailable. Unpin it or refresh the original navigator."),
            ToastVariant::Warning);
        return;
    }
    if (databaseClosePending_ || !workspace_->navigationAllowed() ||
        workspace_->hasPendingEdits() || !allowDocumentChange()) {
        showPinActivationStatus(
            this, key, tr("Finish active database work or pending edits before opening this pin."),
            ToastVariant::Warning);
        return;
    }
    clearPinActivationStatus(this, key);
    ++pinActivationGeneration_;
    pinRevealInFlight_ = false;
    pendingPinKey_ = key;
    pinStartAttempts_ = 0;
    if (!reconnectProfile_) {
        pendingPinKey_.clear();
        showPinActivationStatus(this, key, tr("The saved connection for this pin was not found."),
                                ToastVariant::Danger);
        return;
    }
    if (!reconnectProfile_(found->profileId)) {
        pendingPinKey_.clear();
        showPinActivationStatus(
            this, key,
            tr("The saved connection could not be opened. Fix the connection or finish "
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
    const auto* found = pinFor(pins_, pinRows_, pendingPinKey_);
    if (!found) {
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
                    clearPinActivationStatus(this, key);
                    auto* tree = findChild<QTreeView*>("databaseNavigator");
                    const auto selected = tree ? tree->currentIndex() : QModelIndex{};
                    const auto selectedKind = selected.data(NavigatorModel::KindRole).toString();
                    // A relation's indexes and keys open in its object tab, not the sidebar.
                    const int pane = EngineAdapter::objectKindTraits(pin.kind).detailPane;
                    if (pane >= 0 && EngineAdapter::objectKindTraits(selectedKind).relation &&
                        !EngineAdapter::sidebarChildVisible(selectedKind, pin.kind) &&
                        selected.data(NavigatorModel::ObjectIdRole).toString() ==
                            pin.parentObjectId &&
                        !pin.ancestryIds.isEmpty() &&
                        pin.ancestryIds.last() == pin.parentObjectId) {
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
                    savePinsAsync(std::move(updated),
                                  tr("Could not save unavailable pin state: %1"));
                }
                showPinActivationStatus(this, key, reason, ToastVariant::Warning);
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
        showPinActivationStatus(this, key,
                                tr("The connection is not ready. Activate the pin to retry."),
                                ToastVariant::Warning);
        return;
    }
    QTimer::singleShot(50, this, [this, key] {
        if (pendingPinKey_ == key)
            tryRevealPendingPin();
    });
}

void MainWindow::savePinsAsync(QList<PinRecord> updated, const QString& failureMessage) {
    const auto generation = ++pinSaveGeneration_;
    pins_ = updated;
    pinRows_ = pinRowsFor(pins_);
    renderPins();
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this,
            [this, watcher, generation, updated, failureMessage] {
                const auto error = watcher->result();
                watcher->deleteLater();
                if (error.isEmpty() && generation > pinPersistedGeneration_) {
                    pinPersistedGeneration_ = generation;
                    savedPins_ = updated;
                    const auto priorFailure = property("pinSaveNoticeMessage").toString();
                    if (!priorFailure.isEmpty())
                        clearStatus(QStringLiteral("pins"), priorFailure);
                    setProperty("pinSaveNoticeMessage", QVariant{});
                } else if (!error.isEmpty() && generation == pinSaveGeneration_) {
                    pins_ = savedPins_;
                    pinRows_ = pinRowsFor(pins_);
                    renderPins();
                    const auto message = failureMessage.arg(error);
                    setProperty("pinSaveNoticeMessage", message);
                    showStatus(message, ToastVariant::Danger, QStringLiteral("pins"));
                }
            });
    watcher->setFuture(
        QtConcurrent::run(&pinIoPool_, [store = pinStore_, pins = std::move(updated)] {
            QString error;
            (void)store.save(pins, &error);
            return error;
        }));
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
    savePinsAsync(std::move(updated), tr("The object changed, but its pin could not be saved: %1"));
}
} // namespace choscordb
