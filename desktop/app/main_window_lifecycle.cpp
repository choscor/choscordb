#include "app/main_window.h"

#include "app/appearance_controller.h"
#include "app/editor_preferences.h"
#include "app/main_window_ui.h"
#include "app/main_window_widgets.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
#include "design_system/toast_region/toast_region.h"
#include "widgets/history_dock/history_dock.h"
#include "widgets/search_panel/search_panel.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QWidget>
#include <atomic>

namespace choscordb {

void MainWindow::connectLifecycle(const Ui& ui, const QString& storagePath) {
    const auto fileMenu = ui.fileMenu;
    const auto newQuery = ui.newQuery;
    const auto open = ui.open;
    const auto save = ui.save;
    const auto quit = ui.quit;
    const auto editMenu = ui.editMenu;
    const auto navigator = ui.navigator;
    const auto sidebarPanels = ui.sidebarPanels;
    const auto savedConnections = ui.savedConnections;
    const auto navigatorStatus = ui.navigatorStatus;
    const auto historySearch = ui.historySearch;
    const auto historyStatus = ui.historyStatus;
    const auto historyItems = ui.historyItems;
    const auto resetLayout = ui.resetLayout;
    const auto toolbar = ui.toolbar;
    const auto connections = ui.connections;
    const auto splitter = ui.splitter;
    const auto workspaceTabs = ui.workspaceTabs;
    const auto toolbarHost = ui.toolbarHost;
    const auto searchActions = ui.searchActions;
    const auto toast = ui.toast;
    connect(
        workspace_->adapter(), &EngineAdapter::eventReady, this,
        [connections, navigatorStatus](const BridgeEvent& event) {
            const auto kind =
                QString::fromUtf8(event.kind.data(), static_cast<qsizetype>(event.kind.size()));
            if (kind == "connected") {
                navigatorStatus->setText(tr("● Connected"));
                navigatorStatus->setProperty("state", "success");
            } else if (kind == "disconnected") {
                const bool connected = connections->currentData().isValid();
                navigatorStatus->setText(connected ? tr("● Connected") : tr("○ Disconnected"));
                navigatorStatus->setProperty("state", connected ? "success" : "disconnected");
            } else if (kind == "connection_failed") {
                navigatorStatus->setText(tr("! Connection failed"));
                navigatorStatus->setProperty("state", "error");
            }
            navigatorStatus->setAccessibleName(
                tr("Navigator connection status: %1").arg(navigatorStatus->text()));
            navigatorStatus->style()->unpolish(navigatorStatus);
            navigatorStatus->style()->polish(navigatorStatus);
        },
        Qt::DirectConnection);
    if (!storagePath.isEmpty()) {
        recovery_ = new WorkspaceRecoveryController(editors_, [this] { return addEditor(); }, this);
        recovery_->setObjectFactory([this](const SavedWorkspaceTab& tab) -> QWidget* {
            auto* explorer = initialObjectExplorer_;
            initialObjectExplorer_ = nullptr;
            if (!explorer)
                explorer = makeObjectExplorer();
            explorer->setProperty("objectProfileId", tab.profileId);
            explorer->setProperty("objectConnection", QVariant::fromValue<qulonglong>(0));
            explorer->setProperty("objectId", tab.objectId);
            explorer->setProperty("objectType", tab.objectType);
            explorer->setProperty("objectLabel", tab.label);
            explorer->restoreObject(std::nullopt, tab.objectId, tab.label, tab.objectType);
            explorer->selectPane(static_cast<int>(tab.pane));
            return explorer;
        });
        auto* recoveryStatus = new QWidget(toolbar);
        recoveryStatus->setObjectName("workspaceRecoveryActions");
        auto* recoveryLayout = new QHBoxLayout(recoveryStatus);
        recoveryLayout->setContentsMargins(0, 0, 0, 0);
        auto* recoveryMessage = new QLabel;
        recoveryMessage->setTextFormat(Qt::PlainText);
        recoveryMessage->setFixedWidth(
            recoveryMessage->fontMetrics().horizontalAdvance(tr("Workspace recovery failed")));
        auto* recoveryMenu = new QMenu(tr("Workspace recovery"), fileMenu);
        recoveryMenu->setObjectName("workspaceRecoveryMenu");
        fileMenu->insertMenu(quit, recoveryMenu);
        auto* retry = recoveryMenu->addAction(tr("Retry workspace recovery"));
        retry->setObjectName("retryWorkspaceRecovery");
        auto* startNew = recoveryMenu->addAction(tr("Start new workspace"));
        startNew->setObjectName("startNewWorkspace");
        auto* discardClose = recoveryMenu->addAction(tr("Close without recovery"));
        discardClose->setObjectName("closeWithoutRecovery");
        auto* cancelClose = recoveryMenu->addAction(tr("Keep workspace open"));
        cancelClose->setObjectName("cancelRecoveryClose");
        recoveryLayout->addWidget(recoveryMessage);
        auto* recoveryHeader = new QWidget;
        recoveryHeader->setObjectName("workspaceToolbar");
        auto* recoveryHeaderLayout = new QHBoxLayout(recoveryHeader);
        recoveryHeaderLayout->setContentsMargins(0, 0, 0, 0);
        recoveryHeaderLayout->setSpacing(0);
        recoveryHeaderLayout->addWidget(recoveryStatus);
        recoveryHeaderLayout->addWidget(toolbarHost, 1);
        workspaceTabs->workspaceBar()->setHeader(recoveryHeader);
        recoveryMessage->hide();
        for (auto* action : {retry, startNew, discardClose, cancelClose})
            action->setEnabled(false);
        // Disable the common ancestor so lifecycle updates can still change each
        // action's own enabled state while recovery blocks normal interaction.
        const auto showRecovery = [toolbar, recoveryMessage, retry] {
            toolbar->setEnabled(false);
            recoveryMessage->show();
            retry->setEnabled(true);
        };
        const auto hideRecovery = [toolbar, recoveryMessage, retry, startNew, discardClose,
                                   cancelClose] {
            recoveryMessage->hide();
            for (auto* action : {retry, startNew, discardClose, cancelClose})
                action->setEnabled(false);
            toolbar->setEnabled(true);
        };
        connect(retry, &QAction::triggered, recovery_, &WorkspaceRecoveryController::retry);
        connect(startNew, &QAction::triggered, recovery_, &WorkspaceRecoveryController::startEmpty);
        connect(discardClose, &QAction::triggered, recovery_,
                &WorkspaceRecoveryController::closeWithoutRecovery);
        connect(cancelClose, &QAction::triggered, this, [this, hideRecovery, workspaceTabs] {
            updateInstall_ = {};
            appearanceCloseApproved_ = false;
            recovery_->cancelClose();
            workspaceTabs->workspaceBar()->setProperty("recoveryActive", false);
            workspaceTabs->workspaceBar()->setHeaderVisible(
                qobject_cast<SqlEditor*>(editors_->currentWidget()) != nullptr);
            if (!editors_->count())
                screens_->setCurrentIndex(static_cast<int>(Screen::Start));
            hideRecovery();
        });
        connect(recovery_, &WorkspaceRecoveryController::mutationEnabled, this,
                [newQuery, open, save, editMenu, searchActions](bool enabled) {
                    editMenu->setEnabled(enabled);
                    for (auto* action : searchActions)
                        action->setEnabled(enabled);
                    newQuery->setEnabled(enabled);
                    open->setEnabled(enabled);
                    save->setEnabled(enabled);
                });
        connect(recovery_, &WorkspaceRecoveryController::restoreTabsRequested,
                workspace_->adapter(), &EngineAdapter::restoreWorkspaceTabs);
        connect(recovery_, &WorkspaceRecoveryController::saveTabsRequested, workspace_->adapter(),
                &EngineAdapter::saveWorkspaceTabs);
        connect(workspace_->adapter(), &EngineAdapter::workspaceTabsRestored, recovery_,
                &WorkspaceRecoveryController::restoredTabs);
        connect(workspace_->adapter(), &EngineAdapter::workspaceSaved, recovery_,
                &WorkspaceRecoveryController::saved);
        connect(workspace_->adapter(), &EngineAdapter::recoveryFailed, recovery_,
                &WorkspaceRecoveryController::failed);
        connect(recovery_, &WorkspaceRecoveryController::persistenceSucceeded, this,
                [this, hideRecovery, workspaceTabs] {
                    workspaceTabs->workspaceBar()->setProperty("recoveryActive", false);
                    workspaceTabs->workspaceBar()->setHeaderVisible(
                        qobject_cast<SqlEditor*>(editors_->currentWidget()) != nullptr);
                    hideRecovery();
                });
        connect(recovery_, &WorkspaceRecoveryController::errorOccurred, this,
                [this, showRecovery, recoveryMessage, startNew, discardClose, cancelClose,
                 workspaceTabs](const QString& error, bool closing) {
                    if (closing && updateInstall_) {
                        updateInstall_ = {};
                        appearanceCloseApproved_ = false;
                        recoveryCloseApproved_ = false;
                        recovery_->cancelClose();
                        showToast(tr("Update postponed: %1").arg(error), ToastVariant::Warning);
                        return;
                    }
                    recoveryMessage->setText(recoveryMessage->fontMetrics().elidedText(
                        error, Qt::ElideRight, recoveryMessage->width()));
                    recoveryMessage->setToolTip(error);
                    recoveryMessage->setAccessibleName(error);
                    startNew->setEnabled(!closing && !recovery_->isReady());
                    discardClose->setEnabled(closing);
                    cancelClose->setEnabled(closing);
                    showRecovery();
                    workspaceTabs->workspaceBar()->setProperty("recoveryActive", true);
                    screens_->setCurrentIndex(static_cast<int>(Screen::Sql));
                    workspaceTabs->workspaceBar()->setHeaderVisible(true);
                });
        connect(recovery_, &WorkspaceRecoveryController::closeReady, this, [this] {
            recoveryCloseApproved_ = true;
            QTimer::singleShot(0, this, [this] { close(); });
        });
        connect(recovery_, &WorkspaceRecoveryController::mutationEnabled, search_,
                &QWidget::setEnabled);
        connect(
            recovery_, &WorkspaceRecoveryController::restoreCompleted, this, [this](bool hasTabs) {
                screens_->setCurrentIndex(static_cast<int>(hasTabs ? Screen::Sql : Screen::Start));
                if (auto* object = qobject_cast<ObjectExplorer*>(editors_->currentWidget()))
                    object->activateRestoredObject();
            });
        recovery_->start();
    }
    history_ = new HistoryDock(workspace_->adapter(), this);
    auto filterHistory = [historyItems, historySearch] {
        const auto query = historySearch->text().trimmed();
        for (int i = 0; i < historyItems->count(); ++i) {
            auto* item = historyItems->item(i);
            const auto entry = item->data(Qt::UserRole).value<SavedHistoryEntry>();
            item->setHidden(
                !(item->text() + '\n' + entry.sql).contains(query, Qt::CaseInsensitive));
        }
    };
    connect(historySearch, &QLineEdit::textChanged, this, filterHistory);
    auto refreshRecentHistory = [this, historyStatus, historyItems] {
        if (!workspace_)
            return;
        historyItems->clear();
        historyStatus->show();
        historyStatus->setText(tr("Loading recent history…"));
        static std::atomic<quint64> nextToken{quint64(1) << 62};
        sidebarHistoryToken_ = ++nextToken;
        if (!workspace_->adapter()->listHistory(50, 0, sidebarHistoryToken_))
            historyStatus->setText(tr("Could not request recent history."));
    };
    connect(sidebarPanels, &QStackedWidget::currentChanged, this,
            [refreshRecentHistory](int index) {
                if (index == 2)
                    refreshRecentHistory();
            });
    connect(workspace_->adapter(), &EngineAdapter::historyListed, this,
            [this, historyItems, historyStatus, filterHistory,
             savedConnections](quint64 token, const QList<SavedHistoryEntry>& entries) {
                if (token != sidebarHistoryToken_ || !token)
                    return;
                historyItems->clear();
                for (const auto& entry : entries) {
                    const auto preview = entry.sql.left(100).simplified();
                    const auto when = QDateTime::fromSecsSinceEpoch(entry.timestamp)
                                          .toLocalTime()
                                          .toString(Qt::ISODate);
                    QString profileName =
                        entry.profileId.isEmpty() ? tr("Unsaved connection") : entry.profileId;
                    for (int i = 0; i < savedConnections->count(); ++i) {
                        const auto profileData = savedConnections->item(i)->data(Qt::UserRole);
                        const auto profile = profileData.value<SavedProfile>();
                        if (profile.id == entry.profileId) {
                            profileName = profile.name;
                            break;
                        }
                    }
                    auto* item =
                        new QListWidgetItem(QStringLiteral("%1\n%2 · %3 · %4")
                                                .arg(preview, profileName, when, entry.status),
                                            historyItems);
                    item->setData(Qt::UserRole, QVariant::fromValue(entry));
                }
                filterHistory();
                historyStatus->setVisible(entries.isEmpty());
                historyStatus->setText(
                    entries.isEmpty() ? tr("No query history yet.\n\nRun a query to see it here.")
                                      : QString{});
            });
    connect(workspace_->adapter(), &EngineAdapter::recoveryFailed, this,
            [this, historyStatus](quint64 token, const QString& error) {
                if (token == sidebarHistoryToken_)
                    historyStatus->setText(tr("Recent history could not be loaded: %1").arg(error));
            });
    auto openHistoryItem = [this](QListWidgetItem* item) {
        const auto entry = item->data(Qt::UserRole).value<SavedHistoryEntry>();
        sidebarHistoryOpen_ = true;
        emit history_->openRequested(entry);
        sidebarHistoryOpen_ = false;
    };
    connect(historyItems, &QListWidget::itemClicked, this, openHistoryItem);
    connect(historyItems, &QListWidget::itemActivated, this, openHistoryItem);
    connect(history_, &HistoryDock::noticeRequested, this,
            [this](const QString& message) { showToast(message, ToastVariant::Warning); });
    connect(preferences_, &EditorPreferencesController::historyPolicyConfirmed, history_,
            &HistoryDock::applyConfirmedPolicy);
    history_->hide();
    appearance_ = new AppearanceController(theme_, workspace_->adapter(), this, navigator, splitter,
                                           history_);
    preferences_->setAppearanceController(appearance_);
    connect(resetLayout, &QAction::triggered, appearance_, &AppearanceController::resetLayout);
    connect(appearance_, &AppearanceController::warningChanged, this,
            [toast](const QString& warning) {
                if (warning.isEmpty())
                    toast->clearNotice();
                else
                    toast->showToast(tr("Warning"), warning, ToastVariant::Warning, 0);
            });
    connect(appearance_, &AppearanceController::flushFailed, this, [this](const QString& error) {
        updateInstall_ = {};
        appearanceCloseApproved_ = false;
        showToast(tr("Close postponed: %1").arg(error), ToastVariant::Warning);
    });
    connect(appearance_, &AppearanceController::flushReady, this, [this] {
        appearanceCloseApproved_ = true;
        QTimer::singleShot(0, this, [this] { close(); });
    });
    if (recovery_) {
        history_->setEnabled(recovery_->isReady() && !recovery_->isClosing());
        connect(recovery_, &WorkspaceRecoveryController::mutationEnabled, history_,
                &QWidget::setEnabled);
    }
    connect(history_, &HistoryDock::openRequested, this, [this](const SavedHistoryEntry& entry) {
        if (databaseClosePending_ ||
            (recovery_ && (!recovery_->isReady() || recovery_->isClosing()))) {
            showToast(tr("History cannot be opened while the workspace is unavailable."),
                      ToastVariant::Warning);
            return;
        }
        for (int i = 0; sidebarHistoryOpen_ && i < editors_->count(); ++i) {
            auto* existing = qobject_cast<SqlEditor*>(editors_->widget(i));
            if (existing && !entry.id.isEmpty() &&
                existing->property("historyRecordId").toString() == entry.id) {
                if (!allowDocumentChange())
                    return;
                editors_->setCurrentIndex(i);
                showScreen(Screen::Sql);
                return;
            }
        }
        auto* editor = addEditor();
        if (!editor)
            return;
        if (!editor->restoreDocument(entry.sql.toUtf8(), {}, 0, 0, true)) {
            showToast(tr("History text could not be opened."), ToastVariant::Danger);
            editors_->removeTab(editors_->indexOf(editor));
            editor->deleteLater();
            if (!editors_->count())
                showScreen(Screen::Start);
            return;
        }
        editor->setProfileId(entry.profileId);
        std::optional<quint64> historyConnection;
        QString historyLabel =
            entry.profileId.isEmpty() ? tr("Unavailable connection") : entry.profileId;
        if (auto* selector = findChild<QComboBox*>("connectionSelector")) {
            for (int i = 0; i < selector->count(); ++i) {
                if (!selector->itemData(i).isValid())
                    continue;
                const auto connection = selector->itemData(i).toULongLong();
                if (workspace_->profileIdForConnection(connection) == entry.profileId &&
                    !entry.profileId.isEmpty()) {
                    historyConnection = connection;
                    historyLabel = selector->itemText(i);
                    break;
                }
            }
        }
        editor->setConnectionTarget(historyConnection, historyLabel);
        editor->setProperty("historyRecordId", entry.id);
        workspace_->documentChanged();
        editor->setProperty("documentTitle", tr("History query %1").arg(nextDocumentNumber_));
        editors_->setTabText(editors_->indexOf(editor),
                             editor->property("documentTitle").toString() + " •");
        if (recovery_)
            recovery_->changed();
    });
    connect(workspace_->adapter(), &EngineAdapter::eventReady, this,
            [this, sidebarPanels, refreshRecentHistory](const BridgeEvent& event) {
                const auto kind =
                    QString::fromUtf8(event.kind.data(), static_cast<qsizetype>(event.kind.size()));
                if (history_->isVisible() && (kind == "query_finished" || kind == "query_failed"))
                    history_->refresh();
                if (sidebarPanels->currentIndex() == 2 &&
                    (kind == "query_finished" || kind == "query_failed"))
                    refreshRecentHistory();
            });
    connect(workspace_->adapter(), &EngineAdapter::historyCleared, this,
            [sidebarPanels, refreshRecentHistory](quint64) {
                if (sidebarPanels->currentIndex() == 2)
                    refreshRecentHistory();
            });
    connect(workspace_->adapter(), &EngineAdapter::shutdownReady, this, [this] {
        databaseClosePending_ = false;
        databaseCloseApproved_ = true;
        QTimer::singleShot(0, this, [this] { close(); });
    });
    connect(workspace_->adapter(), &EngineAdapter::shutdownFailed, this,
            [this](const QString& error, bool retryable) {
                QTimer::singleShot(0, this, [this, error, retryable] {
                    setEnabled(true);
                    if (updateInstall_) {
                        updateInstall_ = {};
                        databaseClosePending_ = false;
                        appearanceCloseApproved_ = false;
                        recoveryCloseApproved_ = false;
                        if (recovery_)
                            recovery_->cancelClose();
                        workspace_->cancelShutdown();
                        history_->setEnabled(true);
                        showToast(tr("Update postponed: %1").arg(error), ToastVariant::Warning);
                        return;
                    }
                    ConfirmationDialog box(QMessageBox::Warning, tr("History could not be flushed"),
                                           error, QMessageBox::NoButton, this);
                    box.setTextFormat(Qt::PlainText);
                    auto* retry =
                        retryable ? box.addButton(tr("Retry"), QMessageBox::AcceptRole) : nullptr;
                    auto* discard =
                        box.addButton(tr("Close without history"), QMessageBox::DestructiveRole);
                    auto* cancel = box.addButton(QMessageBox::Cancel);
                    box.setDefaultButton(cancel);
                    box.exec();
                    if (retry && box.clickedButton() == retry) {
                        setEnabled(false);
                        workspace_->beginShutdown();
                    } else if (box.clickedButton() == discard) {
                        workspace_->shutdown();
                        databaseCloseApproved_ = true;
                        databaseClosePending_ = false;
                        QTimer::singleShot(0, this, [this] { close(); });
                    } else {
                        databaseClosePending_ = false;
                        appearanceCloseApproved_ = false;
                        recoveryCloseApproved_ = false;
                        if (recovery_)
                            recovery_->cancelClose();
                        workspace_->cancelShutdown();
                        history_->setEnabled(true);
                    }
                });
            });
}
} // namespace choscordb
