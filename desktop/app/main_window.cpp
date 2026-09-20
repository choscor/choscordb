#include "app/main_window.h"

#include "app/appearance_controller.h"
#include "app/editor_preferences.h"
#include "app/main_window_ui.h"
#include "app/object_data_workspace.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/icons.h"
#include "design_system/platform_accessibility.h"
#include "design_system/theme_manager.h"
#include "design_system/toast_region/toast_region.h"
#include "widgets/editor_completion/editor_completion.h"
#include "widgets/history_dock/history_dock.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyleHints>
#include <QTabWidget>
#include <QTimer>
#include <utility>

int qInitResources_resources();

namespace choscordb {

void MainWindow::showToast(const QString& message, ToastVariant variant) {
    if (toast_) {
        const auto title = variant == ToastVariant::Success   ? tr("Success")
                           : variant == ToastVariant::Warning ? tr("Warning")
                                                              : tr("Error");
        toast_->showToast(title, message, variant);
    }
}

MainWindow::MainWindow(QWidget* parent, const QString& storagePath) : QMainWindow(parent) {
    ::qInitResources_resources();
    theme_ = new design::ThemeManager(this);
    theme_->setSystemPalette(qApp->palette());
    theme_->setSystemAppearance(qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark
                                    ? design::ResolvedAppearance::Dark
                                    : design::ResolvedAppearance::Light);
    theme_->installOn(qApp);
    platformAccessibility_ = new design::PlatformAccessibilityMonitor(theme_, this);
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, theme_,
            [this](Qt::ColorScheme scheme) {
                theme_->setSystemAppearance(scheme == Qt::ColorScheme::Dark
                                                ? design::ResolvedAppearance::Dark
                                                : design::ResolvedAppearance::Light);
            });
    preferences_ = new EditorPreferencesController(this);
    completion_ = new EditorCompletionController(this);
    setWindowTitle(tr("ChoscorDB"));
#ifdef Q_OS_MACOS
    connect(theme_, &design::ThemeManager::themeChanged, this, [this] { updateNativeTitleBar(); });
#endif
    setWindowIcon(design::themedIcon(design::Icon::AppMark, theme_->resolvedTheme().colors.action,
                                     theme_->metrics().iconLarge));
    const auto ui = buildUi();
    connectWorkspace(ui, storagePath);
    connectLifecycle(ui, storagePath);
    connectNavigator(ui);
    constructing_ = false;
    showScreen(Screen::Start);
}
void MainWindow::requestUpdateRestart(std::function<void()> install) {
    if (!install || databaseClosePending_)
        return;
    updateInstall_ = std::move(install);
    close();
}
void MainWindow::finishClose(QCloseEvent* event) {
    if (updateInstall_) {
        event->ignore();
        setEnabled(false);
        QTimer::singleShot(0, this, std::move(updateInstall_));
        updateInstall_ = {};
    } else {
        event->accept();
    }
}
void MainWindow::closeEvent(QCloseEvent* event) {
    for (int i = 0; i < editors_->count(); ++i) {
        if (auto* object = qobject_cast<ObjectExplorer*>(editors_->widget(i))) {
            if (auto* data = object->findChild<ObjectDataWorkspace*>();
                data && !data->resolvePendingEdits()) {
                updateInstall_ = {};
                event->ignore();
                return;
            }
        }
    }
    if (appearance_ && !appearanceCloseApproved_ && !appearance_->flush()) {
        event->ignore();
        return;
    }
    if (databaseCloseApproved_) {
        finishClose(event);
        return;
    }
    if (databaseClosePending_) {
        event->ignore();
        return;
    }
    if (recovery_ && !recoveryCloseApproved_) {
        event->ignore();
        recovery_->requestClose();
        return;
    }
    if (!recovery_) {
        for (int i = 0; i < editors_->count(); ++i) {
            auto* editor = qobject_cast<SqlEditor*>(editors_->widget(i));
            if (editor && editor->isModified()) {
                const auto answer = ConfirmationDialog::question(
                    this, tr("Close workspace"), tr("Discard unsaved changes in the workspace?"),
                    QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
                if (answer != QMessageBox::Discard) {
                    updateInstall_ = {};
                    appearanceCloseApproved_ = false;
                    event->ignore();
                    return;
                }
                break;
            }
        }
    }
    if (workspace_ && !workspace_->confirmShutdown()) {
        updateInstall_ = {};
        appearanceCloseApproved_ = false;
        recoveryCloseApproved_ = false;
        if (recovery_)
            recovery_->cancelClose();
        event->ignore();
        return;
    }
    if (workspace_) {
        databaseClosePending_ = true;
        history_->setEnabled(false);
        setEnabled(false);
        event->ignore();
        workspace_->beginShutdown();
        return;
    }
    finishClose(event);
}
bool MainWindow::allowDocumentChange() {
    if (!workspace_ || workspace_->navigationAllowed())
        return true;
    showToast(tr("Finish or cancel the active database work before changing workspace "
                 "tabs. Cancel remains in the active tab."),
              ToastVariant::Warning);
    return false;
}
bool MainWindow::showScreen(Screen screen) {
    if (screen == Screen::History) {
        if (auto* tab = findChild<QPushButton*>("sidebarHistory")) {
            tab->click();
            return true;
        }
        return false;
    }
    if (!screens_)
        return false;
    if (screen == Screen::Object) {
        if (lastObjectTab_ && editors_->indexOf(lastObjectTab_) >= 0) {
            if (!allowDocumentChange())
                return false;
            editors_->setCurrentWidget(lastObjectTab_);
            screens_->setCurrentIndex(static_cast<int>(Screen::Sql));
            return true;
        }
        for (int i = 0; i < editors_->count(); ++i) {
            if (qobject_cast<ObjectExplorer*>(editors_->widget(i))) {
                if (!allowDocumentChange())
                    return false;
                editors_->setCurrentIndex(i);
                screens_->setCurrentIndex(static_cast<int>(Screen::Sql));
                return true;
            }
        }
        showToast(tr("Open an object from the navigator to show its tab."), ToastVariant::Warning);
        return false;
    }
    if (screen == Screen::Start && editors_->count()) {
        showToast(tr("Close all workspace tabs to return to Start."), ToastVariant::Warning);
        return false;
    }
    if (screen == Screen::Sql && editors_->count() &&
        !qobject_cast<SqlEditor*>(editors_->currentWidget())) {
        if (lastSqlDocument_ && editors_->indexOf(lastSqlDocument_) >= 0) {
            if (!allowDocumentChange())
                return false;
            editors_->setCurrentWidget(lastSqlDocument_);
            screens_->setCurrentIndex(static_cast<int>(Screen::Sql));
            lastSqlDocument_->setFocus();
            return true;
        }
        bool foundSql = false;
        for (int i = 0; i < editors_->count(); ++i) {
            if (qobject_cast<SqlEditor*>(editors_->widget(i))) {
                foundSql = true;
                if (!allowDocumentChange())
                    return false;
                editors_->setCurrentIndex(i);
                break;
            }
        }
        if (!foundSql) {
            addEditor();
            return true;
        }
    }
    if (screens_->currentIndex() != static_cast<int>(screen) && !allowDocumentChange())
        return false;
    if (screen == Screen::Sql && !editors_->count()) {
        addEditor();
        return true;
    }
    screens_->setCurrentIndex(static_cast<int>(screen));
    if (screen == Screen::Sql && editors_->currentWidget())
        editors_->currentWidget()->setFocus();
    return true;
}
ObjectExplorer* MainWindow::makeObjectExplorer() {
    auto* explorer = new ObjectExplorer(workspace_->adapter(), this);
    auto* data = new ObjectDataWorkspace(workspace_, explorer);
    explorer->installDataWidget(data);
    connect(explorer, &ObjectExplorer::dataRequested, data, &ObjectDataWorkspace::openObject);
    connect(explorer, &ObjectExplorer::objectChanged, data, &ObjectDataWorkspace::invalidate);
    connect(explorer, &ObjectExplorer::paneChanged, this, [this](int) {
        if (recovery_)
            recovery_->changed();
    });
    connect(data, &ObjectDataWorkspace::busyChanged, explorer, &ObjectExplorer::setOperationBusy);
    connect(data, &ObjectDataWorkspace::busyChanged, data, [data](bool busy) {
        if (busy)
            progressToast(data)->showProgress(QObject::tr("Object data"),
                                              QObject::tr("Working with object data…"));
        else
            clearProgressToast(data);
    });
    connect(explorer, &ObjectExplorer::sqlGenerated, this,
            [this](quint64 connection, const QString& sql) {
                if (openGeneratedSql_)
                    openGeneratedSql_(connection, sql);
            });
    connect(explorer, &ObjectExplorer::reconnectRequested, this, [this, explorer] {
        const auto context = explorer->property("objectProfileId").toString();
        const bool sessionContext = context.startsWith("session:");
        const auto profileId = context.startsWith("profile:") ? context.mid(8) : QString{};
        auto* selector = findChild<QComboBox*>("connectionSelector");
        std::optional<quint64> target;
        if (selector) {
            for (int i = 0; i < selector->count(); ++i) {
                if (!selector->itemData(i).isValid())
                    continue;
                const auto id = selector->itemData(i).toULongLong();
                if (!sessionContext && !profileId.isEmpty() &&
                    workspace_->profileIdForConnection(id) == profileId) {
                    target = id;
                    break;
                }
            }
        }
        if (!target && sessionContext)
            target = browsingConnection_;
        if (target) {
            const auto linkedProfile = workspace_->profileIdForConnection(*target);
            const auto reboundContext = sessionContext
                                            ? linkedProfile.isEmpty()
                                                  ? QStringLiteral("session:%1").arg(*target)
                                                  : QStringLiteral("profile:%1").arg(linkedProfile)
                                            : QStringLiteral("profile:%1").arg(profileId);
            for (int i = 0; i < editors_->count(); ++i) {
                auto* existing = qobject_cast<ObjectExplorer*>(editors_->widget(i));
                if (!existing || existing == explorer ||
                    existing->property("objectProfileId").toString() != reboundContext ||
                    existing->property("objectType") != explorer->property("objectType") ||
                    existing->property("objectId") != explorer->property("objectId"))
                    continue;
                if (!allowDocumentChange())
                    return;
                editors_->setCurrentWidget(existing);
                existing->selectPane(explorer->paneIndex());
                editors_->removeTab(editors_->indexOf(explorer));
                explorer->deleteLater();
                if (recovery_)
                    recovery_->changed();
                showToast(tr("This object is already open; its tab is selected."),
                          ToastVariant::Warning);
                return;
            }
            explorer->setProperty("objectProfileId", reboundContext);
            explorer->setProperty("objectConnection", QVariant::fromValue<qulonglong>(*target));
            explorer->openObject(*target, explorer->property("objectId").toString(),
                                 explorer->property("objectLabel").toString(),
                                 explorer->property("objectType").toString());
            if (recovery_)
                recovery_->changed();
            return;
        }
        if (!sessionContext && !profileId.isEmpty() && reconnectProfile_) {
            if (reconnectProfile_(profileId))
                showToast(tr("Reconnecting the saved connection. Select this tab to load fresh "
                             "metadata."),
                          ToastVariant::Warning);
            else
                showToast(tr("The saved connection is unavailable. Restore it in the sidebar, "
                             "then retry."),
                          ToastVariant::Danger);
        } else {
            showToast(tr("Select a live connection in the sidebar, then choose Reconnect again."),
                      ToastVariant::Warning);
        }
    });
    return explorer;
}
void MainWindow::openObjectTab(quint64 connection, const QString& objectId, const QString& label,
                               const QString& kind, const QVariantList& properties, int pane) {
    if (!allowDocumentChange() || objectId.isEmpty())
        return;
    const auto profileId = workspace_->profileIdForConnection(connection);
    const auto context = profileId.isEmpty() ? QStringLiteral("session:%1").arg(connection)
                                             : QStringLiteral("profile:%1").arg(profileId);
    for (int i = 0; i < editors_->count(); ++i) {
        auto* explorer = qobject_cast<ObjectExplorer*>(editors_->widget(i));
        if (!explorer || explorer->property("objectId").toString() != objectId ||
            explorer->property("objectType").toString() != kind)
            continue;
        const auto existingContext = explorer->property("objectProfileId").toString();
        if (existingContext != context)
            continue;
        explorer->setProperty("objectProfileId", context);
        editors_->setCurrentIndex(i);
        screens_->setCurrentIndex(static_cast<int>(Screen::Sql));
        explorer->setProperty("objectConnection", QVariant::fromValue<qulonglong>(connection));
        explorer->setProperty("objectLabel", label);
        explorer->openObject(connection, objectId, label, kind, properties);
        if (pane >= 0)
            explorer->selectPane(pane);
        if (recovery_)
            recovery_->changed();
        return;
    }
    auto* explorer = initialObjectExplorer_;
    initialObjectExplorer_ = nullptr;
    if (!explorer)
        explorer = makeObjectExplorer();
    explorer->setProperty("objectProfileId", context);
    explorer->setProperty("objectConnection", QVariant::fromValue<qulonglong>(connection));
    explorer->setProperty("objectId", objectId);
    explorer->setProperty("objectType", kind);
    explorer->setProperty("objectLabel", label);
    explorer->openObject(connection, objectId, label, kind, properties);
    const auto icon =
        design::themedIcon(design::Icon::Table, theme_->resolvedTheme().colors.mutedText, 16);
    const int index = editors_->addTab(explorer, icon, label);
    editors_->setCurrentIndex(index);
    screens_->setCurrentIndex(static_cast<int>(Screen::Sql));
    if (pane >= 0)
        explorer->selectPane(pane);
    if (recovery_)
        recovery_->changed();
}
void MainWindow::openConnectionQuery(quint64 connection) {
    auto* selector = findChild<QComboBox*>("connectionSelector");
    const auto index = selector->findData(QVariant::fromValue<qulonglong>(connection));
    if (index < 0 || !allowDocumentChange())
        return;
    auto* editor = addEditor();
    if (!editor)
        return;
    editor->setConnectionTarget(connection, selector->itemText(index));
    editor->setProfileId(workspace_->profileIdForConnection(connection));
    workspace_->documentChanged();
}
SqlEditor* MainWindow::addEditor() {
    if (!allowDocumentChange())
        return nullptr;
    auto* editor = new SqlEditor;
    preferences_->addEditor(editor);
    connect(editor, &SqlEditor::connectionTargetChanged, this, [this, editor] {
        if (workspace_ && editors_->currentWidget() == editor)
            workspace_->documentChanged();
    });
    const auto title = tr("Untitled query %1").arg(++nextDocumentNumber_);
    editor->setProperty("documentTitle", title);
    if (workspace_) {
        if (const auto* previous = qobject_cast<SqlEditor*>(editors_->currentWidget())) {
            editor->setConnectionTarget(previous->connectionTarget(), previous->targetLabel());
            editor->setProfileId(previous->property("profileId").toString());
        } else if (const auto* object = qobject_cast<ObjectExplorer*>(editors_->currentWidget())) {
            const auto id = object->property("objectConnection").toULongLong();
            auto* selector = findChild<QComboBox*>("connectionSelector");
            const auto index = selector->findData(QVariant::fromValue<qulonglong>(id));
            if (index >= 0) {
                editor->setConnectionTarget(id, selector->itemText(index));
                editor->setProfileId(workspace_->profileIdForConnection(id));
            }
        } else if (auto* selector = findChild<QComboBox*>("connectionSelector")) {
            auto target = selector->currentData().isValid()
                              ? std::optional<quint64>(selector->currentData().toULongLong())
                              : browsingConnection_;
            if (!target) {
                std::optional<quint64> soleConnection;
                for (int i = 0; i < selector->count(); ++i) {
                    if (!selector->itemData(i).isValid())
                        continue;
                    if (soleConnection) {
                        soleConnection.reset();
                        break;
                    }
                    soleConnection = selector->itemData(i).toULongLong();
                }
                target = soleConnection;
            }
            if (target) {
                const auto index = selector->findData(QVariant::fromValue<qulonglong>(*target));
                if (index >= 0) {
                    editor->setConnectionTarget(*target, selector->itemText(index));
                    editor->setProfileId(workspace_->profileIdForConnection(*target));
                }
            }
        }
    }
    const int index = editors_->addTab(
        editor,
        design::themedIcon(design::Icon::Code, theme_->resolvedTheme().colors.mutedText, 16),
        title);
    editors_->setCurrentIndex(index);
    if (!constructing_)
        showScreen(Screen::Sql);
    connect(editor, &SqlEditor::modificationChanged, this, [this, editor](bool modified) {
        auto title = editor->filePath().isEmpty() ? editor->property("documentTitle").toString()
                                                  : QFileInfo(editor->filePath()).fileName();
        if (title.isEmpty())
            title = tr("Untitled query");
        editors_->setTabText(editors_->indexOf(editor), title + (modified ? " •" : ""));
    });
    connect(editor, &SqlEditor::fileOpened, this,
            [this, editor](const QString& path, const QString& error) {
                if (!error.isEmpty()) {
                    ConfirmationDialog box(QMessageBox::Warning, tr("Open failed"), error,
                                           QMessageBox::Ok, this);
                    box.setTextFormat(Qt::PlainText);
                    box.exec();
                } else
                    editors_->setTabText(editors_->indexOf(editor), QFileInfo(path).fileName());
            });
    connect(editor, &SqlEditor::fileSaved, this,
            [this, editor](const QString& path, const QString& error) {
                if (!error.isEmpty()) {
                    ConfirmationDialog box(QMessageBox::Warning, tr("Save failed"), error,
                                           QMessageBox::Ok, this);
                    box.setTextFormat(Qt::PlainText);
                    box.exec();
                } else {
                    editors_->setTabText(editors_->indexOf(editor),
                                         QFileInfo(path).fileName() +
                                             (editor->isModified() ? " •" : ""));
                    if (refreshSavedFiles_)
                        refreshSavedFiles_();
                }
            });
    if (recovery_) {
        recovery_->watchEditor(editor);
        recovery_->changed();
    }
    return editor;
}
} // namespace choscordb
