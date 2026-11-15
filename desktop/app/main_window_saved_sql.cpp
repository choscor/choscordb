#include "app/main_window.h"

#include "app/application_data.h"
#include "app/main_window_ui.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "bridge/rust_text.h"
#include "bridge/text_filter.h"
#include "design_system/menu/menu.h"
#include "models/navigator_model.h"
#include "widgets/filter_field/filter_field.h"
#include "widgets/screen_state/screen_state.h"
#include "widgets/sidebar_section/sidebar_section.h"
#include "widgets/sql_editor/sql_editor.h"
#include "widgets/text_prompt_dialog/text_prompt_dialog.h"
#include <QAction>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QLabel>
#include <QMenu>
#include <QSet>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QtConcurrentRun>
#include <memory>

namespace choscordb {
namespace {
using bridge_detail::fromRust;
using bridge_detail::utf8View;

constexpr int RelativePathRole = Qt::UserRole + 1;

struct SavedSqlEntry {
    QString path;
    QString relativePath;
    bool folder = false;
    QString keyword;
};

struct SavedSqlListing {
    QList<SavedSqlEntry> entries;
    QList<CompletionCandidate> snippets;
    QStringList errors;
    QString error;
    bool hasMore = false;
    quint32 limit = 0;
};

SavedSqlListing loadSavedSql(const QString& root, const QString& sort) {
    SavedSqlListing result;
    if (!root.isValidUtf16()) {
        result.error = QObject::tr("Path is not valid Unicode.");
        return result;
    }
    const auto encoded = root.toUtf8();
    const auto sortBytes = sort.toUtf8();
    const auto dto = saved_sql_list_directory(utf8View(encoded), utf8View(sortBytes));
    result.error = fromRust(dto.error);
    result.hasMore = dto.has_more;
    result.limit = dto.limit;
    result.errors = bridge_detail::fromRustStrings(dto.errors);
    for (const auto& entry : dto.entries)
        result.entries.append({fromRust(entry.path), fromRust(entry.relative_path), entry.folder,
                               fromRust(entry.keyword)});
    for (const auto& snippet : dto.snippets)
        result.snippets.append(
            {fromRust(snippet.label), fromRust(snippet.insert_text), fromRust(snippet.kind)});
    return result;
}

struct SavedSqlIdentity {
    QString path;
    QString error;
};

SavedSqlIdentity documentIdentity(const QString& path) {
    if (!path.isValidUtf16())
        return {{}, QObject::tr("Path is not valid Unicode.")};
    const auto encoded = path.toUtf8();
    const auto dto = saved_sql_document_identity(utf8View(encoded));
    return {fromRust(dto.path), fromRust(dto.error)};
}
}

QString MainWindow::connectSavedSql(const Ui& ui, const QString& storagePath) {
    const auto sidebarPanels = ui.sidebarPanels;
    const auto savedSearch = ui.savedSearch;
    const auto savedStatus = ui.savedStatus;
    const auto savedSection = ui.savedSection;
    const auto savedFiles = ui.savedFiles;
    const auto storageBytes = storagePath.toUtf8();
    const auto defaultDataBytes = applicationDataDirectory().toUtf8();
    const auto savedDirectory =
        fromRust(saved_sql_root(utf8View(storageBytes), utf8View(defaultDataBytes)));
    auto filterSavedFiles = [savedFiles, savedSearch, savedStatus] {
        const TextFilter query(savedSearch->text());
        const auto filterItem = [&](const auto& self, QTreeWidgetItem* item,
                                    const QString& path) -> bool {
            const auto relativePath = path + item->text(0);
            bool matches = query.matches(relativePath);
            for (int i = 0; i < item->childCount(); ++i)
                matches = self(self, item->child(i), relativePath + '/') || matches;
            item->setHidden(!matches);
            if (!query.blank() && matches && item->childCount())
                item->setExpanded(true);
            return matches;
        };
        bool anyMatch = false;
        for (int i = 0; i < savedFiles->topLevelItemCount(); ++i)
            anyMatch = filterItem(filterItem, savedFiles->topLevelItem(i), {}) || anyMatch;
        if (savedFiles->topLevelItemCount() > 0)
            presentPanel(savedStatus, PanelKind::SavedSql,
                         {.loaded = true, .hasRows = anyMatch, .filtered = !query.blank()});
    };
    connect(savedSearch, &FilterField::filterChanged, this, filterSavedFiles);
    const auto savedRefreshGeneration = std::make_shared<quint64>(0);
    const auto sorts = EngineAdapter::savedSqlSorts();
    const auto savedSort = std::make_shared<QString>(sorts.isEmpty() ? QString() : sorts[0].code);
    auto refreshSavedFiles = [this, savedFiles, savedStatus, savedSection, savedDirectory,
                              filterSavedFiles, savedRefreshGeneration, savedSort] {
        const auto generation = ++*savedRefreshGeneration;
        savedFiles->clear();
        savedStatus->hide();
        savedSection->setBusy(
            true,
            EngineAdapter::panelPresentation(PanelKind::SavedSql, PanelState::Loading).message);
        auto* watcher = new QFutureWatcher<SavedSqlListing>(this);
        connect(watcher, &QFutureWatcher<SavedSqlListing>::finished, this,
                [this, watcher, savedFiles, savedStatus, savedSection, filterSavedFiles,
                 savedRefreshGeneration, generation] {
                    const auto result = watcher->result();
                    watcher->deleteLater();
                    if (generation != *savedRefreshGeneration)
                        return;
                    savedSection->setBusy(false);
                    if (!result.error.isEmpty()) {
                        presentPanel(savedStatus, PanelKind::SavedSql, PanelState::Failed,
                                     result.error);
                        return;
                    }
                    QHash<QString, QTreeWidgetItem*> folders;
                    for (const auto& entry : result.entries) {
                        const auto relative = QDir::fromNativeSeparators(entry.relativePath);
                        const auto split = relative.lastIndexOf('/');
                        auto* parent = folders.value(split < 0 ? QString() : relative.left(split),
                                                     savedFiles->invisibleRootItem());
                        auto* item = new QTreeWidgetItem(parent, {relative.mid(split + 1)});
                        item->setData(0, RelativePathRole, relative);
                        item->setToolTip(0,
                                         entry.keyword.isEmpty()
                                             ? relative
                                             : tr("%1\nKeyword: %2").arg(relative, entry.keyword));
                        if (entry.folder) {
                            item->setData(0, NavigatorModel::KindRole, "folder");
                            folders.insert(relative, item);
                        } else {
                            item->setData(0, Qt::UserRole, entry.path);
                            item->setData(0, NavigatorModel::KindRole, "file");
                        }
                    }
                    savedSnippets_ = result.snippets;
                    if (rebuildCompletion_)
                        rebuildCompletion_();
                    presentPanel(savedStatus, PanelKind::SavedSql,
                                 {.loaded = true, .hasRows = !result.entries.isEmpty()});
                    filterSavedFiles();
                    QStringList notes = result.errors;
                    if (result.hasMore)
                        notes.prepend(QObject::tr("Showing the first %1 files.").arg(result.limit));
                    if (!notes.isEmpty()) {
                        savedStatus->setText(notes.join('\n'));
                        savedStatus->show();
                    }
                });
        watcher->setFuture(QtConcurrent::run(
            [savedDirectory, sort = *savedSort] { return loadSavedSql(savedDirectory, sort); }));
    };
    refreshSavedFiles_ = refreshSavedFiles;
    connect(sidebarPanels, &QStackedWidget::currentChanged, this, [refreshSavedFiles](int index) {
        if (index == 1)
            refreshSavedFiles();
    });
    const auto pendingSavedOpens = std::make_shared<QSet<QString>>();
    const auto savedOpenGeneration = std::make_shared<quint64>(0);
    auto openSavedItem = [this, savedDirectory, pendingSavedOpens,
                          savedOpenGeneration](QTreeWidgetItem* item) {
        const auto path = item->data(0, Qt::UserRole).toString();
        if (path.isEmpty() || pendingSavedOpens->contains(path))
            return;
        if (!allowDocumentChange() || databaseClosePending_ ||
            (recovery_ && (!recovery_->isReady() || recovery_->isClosing()))) {
            reportNotice(tr("Saved file cannot be opened while the workspace is busy."));
            return;
        }
        pendingSavedOpens->insert(path);
        const auto generation = ++*savedOpenGeneration;
        QList<QPointer<SqlEditor>> openEditors;
        QStringList openPaths;
        for (int i = 0; i < editors_->count(); ++i) {
            auto* existing = qobject_cast<SqlEditor*>(editors_->widget(i));
            if (!existing || existing->filePath().isEmpty())
                continue;
            openEditors.append(existing);
            openPaths.append(existing->filePath());
        }
        auto* watcher = new QFutureWatcher<QList<SavedSqlIdentity>>(this);
        connect(watcher, &QFutureWatcher<QList<SavedSqlIdentity>>::finished, this,
                [this, watcher, openEditors, openPaths, path, savedDirectory, pendingSavedOpens,
                 savedOpenGeneration, generation] {
                    const auto identities = watcher->result();
                    watcher->deleteLater();
                    const bool focusRequested = generation == *savedOpenGeneration;
                    if (!allowDocumentChange() || databaseClosePending_ ||
                        (recovery_ && (!recovery_->isReady() || recovery_->isClosing()))) {
                        pendingSavedOpens->remove(path);
                        reportNotice(
                            tr("Saved file cannot be opened while the workspace is busy."));
                        return;
                    }
                    if (identities.isEmpty() || !identities.front().error.isEmpty()) {
                        pendingSavedOpens->remove(path);
                        reportError(tr("Could not open %1: %2")
                                        .arg(QFileInfo(path).fileName(),
                                             identities.isEmpty() ? tr("Path is invalid.")
                                                                  : identities.front().error));
                        return;
                    }
                    for (int i = 0; i < openEditors.size(); ++i) {
                        auto* existing = openEditors.at(i).data();
                        if (existing && existing->filePath() == openPaths.at(i) &&
                            identities.at(i + 1).error.isEmpty() &&
                            identities.at(i + 1).path == identities.front().path) {
                            pendingSavedOpens->remove(path);
                            if (focusRequested) {
                                editors_->setCurrentWidget(existing);
                                showScreen(Screen::Sql);
                            }
                            return;
                        }
                    }
                    const QPointer<QWidget> previouslyFocused = editors_->currentWidget();
                    if (auto* editor = addEditor()) {
                        connect(editor, &SqlEditor::fileOpened, this,
                                [this, editor, path, pendingSavedOpens](const QString& openedPath,
                                                                        const QString& error) {
                                    pendingSavedOpens->remove(path);
                                    editor->setProperty("savedSqlOpen", false);
                                    if (error.isEmpty())
                                        return;
                                    if (editors_->indexOf(editor) < 0)
                                        return;
                                    reportError(tr("Could not open %1: %2")
                                                    .arg(QFileInfo(openedPath).fileName(), error));
                                    editors_->removeTab(editors_->indexOf(editor));
                                    editor->deleteLater();
                                    if (!editors_->count())
                                        showScreen(Screen::Start);
                                });
                        connect(editor, &QObject::destroyed, this,
                                [pendingSavedOpens, path] { pendingSavedOpens->remove(path); });
                        editor->setProperty("savedSqlOpen", true);
                        editor->openSavedFile(savedDirectory, path);
                        if (!focusRequested && previouslyFocused &&
                            editors_->indexOf(previouslyFocused) >= 0)
                            editors_->setCurrentWidget(previouslyFocused);
                    } else
                        pendingSavedOpens->remove(path);
                });
        watcher->setFuture(QtConcurrent::run([path, openPaths] {
            QList<SavedSqlIdentity> identities;
            identities.reserve(openPaths.size() + 1);
            identities.append(documentIdentity(path));
            for (const auto& openPath : openPaths)
                identities.append(documentIdentity(openPath));
            return identities;
        }));
    };
    connect(savedFiles, &QTreeWidget::itemClicked, this, openSavedItem);
    connect(savedFiles, &QTreeWidget::itemActivated, this, openSavedItem);
    savedFiles->setContextMenuPolicy(Qt::CustomContextMenu);
    const auto prompt = [this, refreshSavedFiles](const TextPrompt& text,
                                                  TextPromptDialog::Work work) {
        auto* dialog = new TextPromptDialog(text, std::move(work), this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &TextPromptDialog::applied, this, refreshSavedFiles);
        dialog->open();
    };
    connect(
        savedFiles, &QWidget::customContextMenuRequested, this,
        [this, savedFiles, openSavedItem, savedDirectory, savedSort, sorts, refreshSavedFiles,
         prompt](const QPoint& point) {
            auto* item = savedFiles->itemAt(point);
            const auto path = item ? item->data(0, Qt::UserRole).toString() : QString();
            const auto relative = item ? item->data(0, RelativePathRole).toString() : QString();
            QMenu menu(savedFiles);
            menu.setObjectName("sidebarSavedMenu");
            if (!path.isEmpty()) {
                connect(menu.addAction(tr("Open")), &QAction::triggered, savedFiles,
                        [item, openSavedItem] { openSavedItem(item); });
                connect(menu.addAction(tr("Copy path")), &QAction::triggered, savedFiles,
                        [path] { QGuiApplication::clipboard()->setText(path); });
                auto* keyword = menu.addAction(tr("Set keyword…"));
                keyword->setObjectName("savedSetKeyword");
                connect(keyword, &QAction::triggered, this, [this, path, savedDirectory, prompt] {
                    for (int i = 0; i < editors_->count(); ++i)
                        if (auto* editor = qobject_cast<SqlEditor*>(editors_->widget(i));
                            editor && editor->filePath() == path) {
                            reportError(tr("Close %1 before you change its keyword.")
                                            .arg(QFileInfo(path).fileName()));
                            return;
                        }
                    prompt({"savedKeywordPrompt",
                            tr("Set keyword"),
                            tr("Type the keyword in the SQL editor to insert this query. An "
                               "empty keyword removes it."),
                            tr("Keyword"),
                            {},
                            tr("Save keyword")},
                           [savedDirectory, path](const QString& text) {
                               return EngineAdapter::savedSqlSetKeyword(savedDirectory, path, text);
                           });
                });
            } else {
                const auto parent = relative;
                auto* folder = menu.addAction(tr("New folder…"));
                folder->setObjectName("savedNewFolder");
                connect(folder, &QAction::triggered, this, [savedDirectory, parent, prompt] {
                    prompt({"savedFolderPrompt",
                            tr("New folder"),
                            tr("Make a folder for saved queries."),
                            tr("Name"),
                            {},
                            tr("Create folder")},
                           [savedDirectory, parent](const QString& text) {
                               return EngineAdapter::savedSqlCreateFolder(savedDirectory, parent,
                                                                          text);
                           });
                });
            }
            menu.addSeparator();
            for (const auto& sort : sorts) {
                auto* action = menu.addAction(sort.label);
                action->setObjectName("savedSort_" + sort.code);
                action->setCheckable(true);
                action->setChecked(*savedSort == sort.code);
                connect(action, &QAction::triggered, this,
                        [savedSort, code = sort.code, refreshSavedFiles] {
                            *savedSort = code;
                            refreshSavedFiles();
                        });
            }
            design::execContextMenu(menu, savedFiles->viewport()->mapToGlobal(point));
        });
    refreshSavedFiles();
    return savedDirectory;
}
}
