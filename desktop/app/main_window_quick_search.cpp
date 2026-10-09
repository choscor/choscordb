#include "app/main_window.h"
#include "bridge/request_token.h"

#include "app/editor_preferences.h"
#include "app/navigator_controller.h"
#include "app/object_explorer.h"
#include "app/object_kind_icon.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "bridge/quick_search.h"
#include "bridge/text_filter.h"
#include "design_system/quick_search/quick_search_dialog.h"
#include "models/navigator_model.h"
#include "widgets/history_dock/history_dock.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QDateTime>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <algorithm>

namespace choscordb {
namespace {
QString relationSubtype(const QVariantList& properties) {
    return NavigatorModel::relationSubtype(properties);
}

QString matchSnippet(const QString& text, qsizetype match, qsizetype length) {
    constexpr qsizetype width = 120;
    const qsizetype start = qMax<qsizetype>(0, match - 40);
    const qsizetype end = qMin(text.size(), qMax(start + width, match + length + 20));
    return (start ? QStringLiteral("…") : QString{}) + text.mid(start, end - start).simplified() +
           (end < text.size() ? QStringLiteral("…") : QString{});
}
} // namespace

void MainWindow::showQuickSearch() {
    if (!quickSearch_) {
        quickSearch_ = new design::QuickSearchDialog(this);
        connect(quickSearch_, &design::QuickSearchDialog::queryChanged, this,
                &MainWindow::updateQuickSearch);
        connect(quickSearch_, &design::QuickSearchDialog::activated, this,
                &MainWindow::activateQuickSearch);
        connect(editors_, &QTabWidget::tabCloseRequested, this, [this] {
            if (quickSearch_->isVisible())
                QTimer::singleShot(0, this, &MainWindow::refreshQuickSearchIfOpen);
        });
        connect(quickSearch_, &QDialog::finished, this, [this] {
            ++quickSearchGeneration_;
            quickHistoryToken_ = 0;
            quickHistoryPolicyToken_ = 0;
            quickHistoryPending_ = false;
            quickPendingRecentObject_.clear();
            quickRecentStatus_.clear();
            if (navigatorController_)
                navigatorController_->cancelQuickObjectSearch();
        });
        connect(
            workspace_->adapter(), &EngineAdapter::historySearched, this,
            [this](quint64 token, const QList<SavedHistoryEntry>& entries, bool incomplete,
                   quint64 nextOffset) {
                if (!quickSearch_ || !quickSearch_->isVisible() || token != quickHistoryToken_)
                    return;
                quickHistoryToken_ = 0;
                for (const auto& entry : entries) {
                    const QString id = QStringLiteral("history:%1").arg(entry.id);
                    const QString when = QDateTime::fromSecsSinceEpoch(entry.timestamp)
                                             .toLocalTime()
                                             .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                    const auto found = quickTextFinder_ ? quickTextFinder_->findAll(entry.sql, 1)
                                                        : QList<TextSpan>{};
                    const auto match = found.isEmpty() ? TextSpan{} : found.first();
                    quickHistoryRows_.append(
                        {tr("History"), matchSnippet(entry.sql, match.start, match.length),
                         tr("%1 · %2 · %3 · %4")
                             .arg(entry.profileId.isEmpty() ? tr("Unsaved connection")
                                                            : entry.profileId,
                                  when, entry.status, entry.id.left(8)),
                         id, design::Icon::Refresh});
                    quickHistoryMatches_.insert(id, QVariant::fromValue(entry));
                }
                const bool progressed = nextOffset != 0 && nextOffset != quickHistoryCursor_;
                const auto historyRows = QuickSearchNeedle::limits().historyRows;
                if (incomplete && quickHistoryRows_.size() < historyRows && progressed) {
                    quickHistoryCursor_ = nextOffset;
                    quickHistoryToken_ = nextRequestToken();
                    if (workspace_->adapter()->searchHistory(quickSearch_->query().trimmed(),
                                                             historyRows - quickHistoryRows_.size(),
                                                             quickHistoryToken_, nextOffset)) {
                        renderQuickSearch();
                        return;
                    }
                    quickHistoryToken_ = 0;
                    quickHistoryError_ = tr("History search could not continue. Retry.");
                }
                quickHistoryPending_ = false;
                quickHistoryReady_ = true;
                quickHistoryIncomplete_ = incomplete && quickHistoryRows_.size() >= historyRows;
                if (incomplete && !progressed)
                    quickHistoryError_ = tr("History search stopped. Retry the query.");
                renderQuickSearch();
            });
        connect(
            workspace_->adapter(), &EngineAdapter::recoveryFailed, this,
            [this](quint64 token, const QString& error) {
                if (token == quickHistoryPolicyToken_) {
                    quickHistoryPolicyToken_ = 0;
                    quickHistoryPolicyError_ = tr("History recording status is unavailable.");
                    renderQuickSearch();
                    return;
                }
                if (token == quickHistoryClearToken_ ||
                    (quickHistoryClearing_ && !workspace_->adapter()->historyClearInProgress())) {
                    quickHistoryClearToken_ = 0;
                    quickHistoryClearing_ = workspace_->adapter()->historyClearInProgress();
                    refreshQuickSearchIfOpen();
                    quickHistoryError_ = tr("History clear failed: %1. Retry.").arg(error);
                    renderQuickSearch();
                    return;
                }
                if (!quickSearch_ || !quickSearch_->isVisible() || token != quickHistoryToken_)
                    return;
                quickHistoryToken_ = 0;
                quickHistoryPending_ = false;
                quickHistoryError_ = tr("History search failed: %1. Refine or retry.").arg(error);
                renderQuickSearch();
            });
        connect(workspace_->adapter(), &EngineAdapter::historyClearStarted, this,
                [this](quint64 token) {
                    quickHistoryClearToken_ = token;
                    quickHistoryClearing_ = true;
                    if (!quickSearch_ || !quickSearch_->isVisible())
                        return;
                    quickHistoryToken_ = 0;
                    quickHistoryPending_ = false;
                    quickHistoryReady_ = false;
                    quickHistoryRows_.clear();
                    quickHistoryMatches_.clear();
                    renderQuickSearch();
                });
        connect(workspace_->adapter(), &EngineAdapter::historyCleared, this, [this](quint64 token) {
            if (token == quickHistoryClearToken_)
                quickHistoryClearToken_ = 0;
            quickHistoryClearing_ = workspace_->adapter()->historyClearInProgress();
            refreshQuickSearchIfOpen();
        });
        connect(workspace_->adapter(), &EngineAdapter::historyPolicyReady, this,
                [this](quint64 token, const HistoryPolicy& policy) {
                    if (token != quickHistoryPolicyToken_)
                        return;
                    quickHistoryPolicyToken_ = 0;
                    quickHistoryPolicyKnown_ = true;
                    quickHistoryRecordingEnabled_ = policy.enabled;
                    quickHistoryPolicyError_.clear();
                    renderQuickSearch();
                });
        connect(preferences_, &EditorPreferencesController::historyPolicyConfirmed, this,
                [this](const HistoryPolicy& policy) {
                    quickHistoryPolicyKnown_ = true;
                    quickHistoryRecordingEnabled_ = policy.enabled;
                    if (quickSearch_ && quickSearch_->isVisible())
                        renderQuickSearch();
                });
        connect(navigatorController_, &NavigatorController::quickObjectSearchChanged, this,
                &MainWindow::updateQuickObjectRows);
        connect(navigatorController_, &NavigatorController::selectedConnectionsChanged, this,
                &MainWindow::refreshQuickSearchIfOpen);
        connect(navigatorController_, &NavigatorController::browsingVisibilityChanged, this,
                &MainWindow::refreshQuickSearchIfOpen);
        // Recent objects are re-verified against loaded metadata; fold page bursts.
        const auto refreshRecentObjects = coalescedCall(quickSearch_, 250, [this] {
            if (quickSearch_ && quickSearch_->isVisible() &&
                quickSearch_->query().trimmed().isEmpty() && quickPendingRecentObject_.isEmpty())
                updateQuickSearch(quickSearch_->query());
        });
        connect(navigatorController_->model(), &NavigatorModel::completionChanged, this,
                [this, refreshRecentObjects](quint64 connection) {
                    if (quickSearch_ && quickSearch_->isVisible() &&
                        quickSearch_->query().trimmed().isEmpty() &&
                        quickPendingRecentObject_.isEmpty() &&
                        quickSearchConnection() == connection)
                        refreshRecentObjects();
                });
        connect(this, &MainWindow::browsingConnectionChanged, this,
                &MainWindow::refreshQuickSearchIfOpen);
        if (auto* action = findChild<QAction*>("quickSwitch"))
            connect(action, &QAction::changed, quickSearch_,
                    [this, action] { quickSearch_->setReopenShortcut(action->shortcut()); });
    }
    if (auto* action = findChild<QAction*>("quickSwitch"))
        quickSearch_->setReopenShortcut(action->shortcut());
    if (!quickSearch_->isVisible() && workspace_) {
        quickHistoryClearing_ = workspace_->adapter()->historyClearInProgress();
        if (!quickHistoryClearing_)
            quickHistoryClearToken_ = 0;
        quickHistoryPolicyKnown_ = false;
        quickHistoryPolicyError_.clear();
        quickHistoryPolicyToken_ = nextRequestToken();
        if (!workspace_->adapter()->getHistoryPolicy(quickHistoryPolicyToken_)) {
            quickHistoryPolicyToken_ = 0;
            quickHistoryPolicyError_ = tr("History recording status is unavailable.");
        }
    }
    quickSearch_->openSearch();
}

void MainWindow::updateQuickSearch(const QString& query) {
    if (!quickSearch_)
        return;
    const quint64 generation = ++quickSearchGeneration_;
    quickEditorRows_.clear();
    quickEditorMatches_.clear();
    quickEditorRevisions_.clear();
    quickEditorIncomplete_ = false;
    quickEditorPending_ = false;
    quickHistoryToken_ = 0;
    quickHistoryCursor_ = 0;
    quickHistoryRows_.clear();
    quickHistoryMatches_.clear();
    quickHistoryPending_ = false;
    quickHistoryIncomplete_ = false;
    quickHistoryReady_ = false;
    quickHistoryError_.clear();
    quickObjectRows_.clear();
    quickObjectMatches_.clear();
    quickObjectIncomplete_ = false;
    quickObjectStatus_.clear();
    quickPendingRecentObject_.clear();
    quickRecentStatus_.clear();
    const QString needle = query.trimmed();
    const bool empty = needle.isEmpty();
    quickTextFinder_ = std::make_shared<const TextFinder>(needle);
    // Rust ranks these rows and drops the ones that do not match in renderQuickSearch.
    quickNameRows_.clear();
    quickTabTargets_.clear();
    for (const auto& name : {"showStart", "showSql", "showObjects", "showHistory"})
        if (auto* action = findChild<QAction*>(name))
            quickNameRows_.append({tr("Screen"), action->text(), tr("View"),
                                   QString::fromLatin1(name), design::Icon::Square});
    for (int index = 0; editors_ && index < editors_->count(); ++index) {
        const QString title = editors_->tabText(index);
        const QString id = QStringLiteral("tab:%1").arg(index);
        quickTabTargets_.insert(id, editors_->widget(index));
        QString context = tr("Workspace tab %1").arg(index + 1);
        if (auto* editor = qobject_cast<SqlEditor*>(editors_->widget(index));
            editor && !editor->filePath().isEmpty())
            context += QStringLiteral(" · ") + editor->filePath();
        else if (auto* object = qobject_cast<ObjectExplorer*>(editors_->widget(index)))
            context += QStringLiteral(" · ") + object->property("objectProfileId").toString();
        const auto icon =
            qobject_cast<ObjectExplorer*>(editors_->widget(index))
                ? objectKindIcon(editors_->widget(index)->property("objectType").toString())
                : design::Icon::Code;
        quickNameRows_.append({tr("Tab"), title, context, id, icon});
    }
    const auto connection = quickSearchConnection();
    // Milliseconds of typing quiet before the navigator object search starts.
    constexpr int quickObjectDebounceMs = 80;
    if (navigatorController_) {
        if (empty)
            navigatorController_->cancelQuickObjectSearch();
        else
            // Debounced like history: each keystroke supersedes the pending scan.
            navigatorController_->startQuickObjectSearch(needle, connection, quickObjectDebounceMs);
    }
    if (empty && connection) {
        int ordinal = 0;
        for (const auto& object : quickRecentObjects_) {
            if (object.value(QStringLiteral("connection")).toULongLong() != *connection)
                continue;
            auto* model = navigatorController_->model();
            if (object.value(QStringLiteral("unverified")).toBool()) {
                if (!model->canShowUnverifiedObject(
                        *connection, object.value(QStringLiteral("qualifiedName")).toString()))
                    continue;
            } else {
                const auto snapshot = model->objectSnapshot(
                    *connection, object.value(QStringLiteral("objectId")).toString());
                if (!snapshot ||
                    snapshot->name != object.value(QStringLiteral("name")).toString() ||
                    snapshot->qualifiedName !=
                        object.value(QStringLiteral("qualifiedName")).toString() ||
                    snapshot->kind != object.value(QStringLiteral("kind")).toString() ||
                    snapshot->parentObjectId !=
                        object.value(QStringLiteral("parentObjectId")).toString() ||
                    relationSubtype(snapshot->properties) !=
                        relationSubtype(object.value(QStringLiteral("properties")).toList()))
                    continue;
            }
            const QString id = QStringLiteral("recent-object:%1:%2").arg(generation).arg(++ordinal);
            quickObjectRows_.append(
                {tr("Object"), object.value(QStringLiteral("name")).toString(),
                 tr("Recent · %1 · %2 · connection %3")
                     .arg(object.value(QStringLiteral("kind")).toString())
                     .arg(object.value(QStringLiteral("qualifiedName")).toString())
                     .arg(object.value(QStringLiteral("connection")).toULongLong()),
                 id, objectKindIcon(object.value(QStringLiteral("kind")).toString())});
            quickObjectMatches_.insert(id, object);
        }
    }
    quickEditorPending_ = !empty && editors_ && editors_->count() > 0;
    quickHistoryPending_ =
        !quickHistoryClearing_ && workspace_ && history_ && history_->isEnabled();
    if (quickHistoryPending_) {
        QTimer::singleShot(empty ? 0 : 80, this, [this, generation, needle] {
            if (!quickSearch_ || !quickSearch_->isVisible() || generation != quickSearchGeneration_)
                return;
            quickHistoryToken_ = nextRequestToken();
            if (!workspace_->adapter()->searchHistory(
                    needle, QuickSearchNeedle::limits().historyRows, quickHistoryToken_)) {
                quickHistoryToken_ = 0;
                quickHistoryPending_ = false;
                quickHistoryError_ = tr("History search is unavailable. Retry the query.");
                renderQuickSearch();
            }
        });
    } else if (history_ && !quickHistoryClearing_) {
        quickHistoryError_ = tr("History is unavailable while the workspace is recovering.");
    }
    renderQuickSearch();
    // ui-budget: characters of open editor text one quick search reads across passes.
    constexpr int editorScanChars = 1024 * 1024;
    if (!empty && editors_ && editors_->count() > 0)
        QTimer::singleShot(0, this, [this, generation] {
            scanQuickSearchEditors(generation, 0, 0, editorScanChars);
        });
}

void MainWindow::renderQuickSearch() {
    if (!quickSearch_)
        return;
    const QString needle = quickSearch_->query().trimmed();
    const QuickSearchNeedle matcher(needle);
    QList<const design::QuickSearchResult*> rows;
    QList<QuickSearchDestination> destinations;
    rows.reserve(quickNameRows_.size() + quickObjectRows_.size());
    destinations.reserve(rows.capacity());
    const auto add = [&](const design::QuickSearchResult& row, QuickSearchDestination::Kind kind) {
        rows.append(&row);
        destinations.append({kind, row.type, row.title, row.context, row.id});
    };
    for (const auto& row : quickNameRows_)
        add(row, row.id.startsWith(QStringLiteral("tab:")) ? QuickSearchDestination::Kind::OpenTab
                                                           : QuickSearchDestination::Kind::Command);
    for (const auto& row : quickObjectRows_)
        add(row, QuickSearchDestination::Kind::Object);
    const auto plan = matcher.plan(destinations, quickEditorRows_.size(), quickHistoryRows_.size());
    QList<design::QuickSearchResult> results;
    results.reserve(plan.destinations.size() + plan.editorRows + plan.historyRows);
    for (const auto index : plan.destinations)
        results.append(*rows[index]);
    results.append(quickEditorRows_.mid(0, plan.editorRows));
    results.append(quickHistoryRows_.mid(0, plan.historyRows));
    quickSearch_->setResults(results);
    quickSearch_->setLoading(quickEditorPending_ || quickHistoryPending_);
    quickSearch_->setError(quickHistoryError_);
    QStringList status;
    if (quickEditorIncomplete_)
        status << tr("Some SQL text was skipped. Refine the query to search again.");
    if (quickHistoryIncomplete_)
        status << tr("History results are incomplete. Refine the query.");
    if (quickHistoryClearing_)
        status << tr("History is being cleared.");
    else if (quickHistoryReady_ && quickHistoryRows_.isEmpty())
        status << (needle.isEmpty() ? tr("No saved history yet.")
                                    : tr("No saved history matches this search."));
    if (quickHistoryPolicyKnown_ && !quickHistoryRecordingEnabled_)
        status << tr("History recording is off. Saved history remains searchable.");
    if (!quickHistoryPolicyError_.isEmpty())
        status << quickHistoryPolicyError_;
    if (!quickObjectStatus_.isEmpty())
        status << quickObjectStatus_;
    if (!quickRecentStatus_.isEmpty())
        status << quickRecentStatus_;
    if (plan.more)
        status << tr("More results exist. Refine the query.");
    quickSearch_->setStatus(status.join(QStringLiteral("  ")));
}

void MainWindow::updateQuickObjectRows() {
    if (!quickSearch_ || !quickSearch_->isVisible() || !navigatorController_)
        return;
    if (quickSearch_->query().trimmed().isEmpty()) {
        if (quickPendingRecentObject_.isEmpty())
            return;
        const auto pending = quickPendingRecentObject_;
        const quint64 connection = pending.value(QStringLiteral("connection")).toULongLong();
        if (quickSearchConnection() != connection) {
            quickPendingRecentObject_.clear();
            quickRecentStatus_ =
                tr("The selected connection changed. Search for the object again.");
            renderQuickSearch();
            return;
        }
        for (const auto& result : navigatorController_->quickObjectResults()) {
            if (result.connection != connection ||
                result.objectId != pending.value(QStringLiteral("objectId")).toString() ||
                result.kind != pending.value(QStringLiteral("kind")).toString() ||
                result.qualifiedName != pending.value(QStringLiteral("qualifiedName")).toString())
                continue;
            const auto snapshot =
                navigatorController_->model()->objectSnapshot(connection, result.objectId);
            if (!snapshot)
                continue;
            quickPendingRecentObject_.clear();
            quickRecentStatus_.clear();
            if (!allowDocumentChange()) {
                quickSearch_->setStatus(
                    tr("Finish or cancel active work before opening an object."));
                return;
            }
            openObjectTab(connection, snapshot->objectId, snapshot->qualifiedName, snapshot->kind,
                          snapshot->properties);
            auto* current = qobject_cast<ObjectExplorer*>(editors_->currentWidget());
            if (current && current->property("objectId").toString() == snapshot->objectId)
                quickSearch_->reject();
            else
                quickSearch_->setStatus(tr("The recent object could not be opened."));
            return;
        }
        if (!navigatorController_->quickObjectSearching()) {
            quickPendingRecentObject_.clear();
            quickRecentStatus_ =
                navigatorController_->quickObjectSearchIncomplete()
                    ? tr("The recent object could not be verified. Refine the search.")
                    : tr("The recent object is no longer available.");
        } else {
            quickRecentStatus_ = tr("Checking the recent object…");
        }
        renderQuickSearch();
        return;
    }
    quickObjectRows_.clear();
    quickObjectMatches_.clear();
    quickObjectStatus_ = navigatorController_->quickObjectSearchStatus();
    quickObjectIncomplete_ = navigatorController_->quickObjectSearchIncomplete();
    for (const auto& object : navigatorController_->quickObjectResults()) {
        // A stable row key keeps the palette selection while results stream in.
        QStringList identity;
        for (const auto& part :
             {QString::number(object.connection), object.objectId, object.kind,
              object.qualifiedName, object.parentObjectId, relationSubtype(object.properties)})
            identity << QString::fromLatin1(part.toUtf8().toBase64(QByteArray::Base64UrlEncoding |
                                                                   QByteArray::OmitTrailingEquals));
        const QString id = QStringLiteral("object:") + identity.join('.');
        quickObjectRows_.append(
            {tr("Object"), object.name,
             tr("%1 · %2 · %3").arg(object.context, object.qualifiedName, object.kind), id,
             objectKindIcon(object.kind)});
        quickObjectMatches_.insert(
            id, {{QStringLiteral("connection"), QVariant::fromValue<qulonglong>(object.connection)},
                 {QStringLiteral("objectId"), object.objectId},
                 {QStringLiteral("name"), object.name},
                 {QStringLiteral("qualifiedName"), object.qualifiedName},
                 {QStringLiteral("kind"), object.kind},
                 {QStringLiteral("parentObjectId"), object.parentObjectId},
                 {QStringLiteral("properties"), object.properties},
                 {QStringLiteral("targetObjectId"), object.targetObjectId},
                 {QStringLiteral("targetQualifiedName"), object.targetQualifiedName},
                 {QStringLiteral("targetKind"), object.targetKind},
                 {QStringLiteral("targetParentObjectId"), object.targetParentObjectId},
                 {QStringLiteral("targetProperties"), object.targetProperties},
                 {QStringLiteral("targetPane"), object.targetPane}});
    }
    renderQuickSearch();
}

void MainWindow::scanQuickSearchEditors(quint64 generation, int tabIndex, int line,
                                        int remainingChars) {
    if (!quickSearch_ || !quickSearch_->isVisible() || generation != quickSearchGeneration_ ||
        !quickTextFinder_)
        return;
    int scannedLines = 0;
    int scannedChars = 0;
    while (tabIndex < editors_->count() && remainingChars > 0 &&
           quickEditorRows_.size() < QuickSearchNeedle::limits().editorRows) {
        auto* editor = qobject_cast<SqlEditor*>(editors_->widget(tabIndex));
        if (!editor) {
            ++tabIndex;
            line = 0;
            continue;
        }
        if (!quickEditorRevisions_.contains(editor))
            quickEditorRevisions_.insert(editor, editor->revision());
        if (quickEditorRevisions_.value(editor) != editor->revision()) {
            updateQuickSearch(quickSearch_->query());
            return;
        }
        if (line >= editor->lines()) {
            ++tabIndex;
            line = 0;
            continue;
        }
        const int length = static_cast<int>(editor->SendScintilla(
            QsciScintillaBase::SCI_LINELENGTH, static_cast<unsigned long>(line)));
        if (length > 8192) {
            quickEditorIncomplete_ = true;
            remainingChars -= qMin(length, remainingChars);
            ++line;
            continue;
        }
        if (length > remainingChars) {
            quickEditorIncomplete_ = true;
            break;
        }
        const QString text = editor->text(line);
        for (const auto& match : quickTextFinder_->findAll(
                 text, int(QuickSearchNeedle::limits().editorRows - quickEditorRows_.size()))) {
            const QString id =
                QStringLiteral("sql:%1:%2").arg(generation).arg(quickEditorMatches_.size());
            quickEditorMatches_.insert(id, {editor, editor->revision(), line,
                                            static_cast<int>(match.byteStart),
                                            static_cast<int>(match.byteLength)});
            quickEditorRows_.append(
                {tr("SQL text"), matchSnippet(text, match.start, match.length),
                 tr("%1 · line %2").arg(editors_->tabText(tabIndex)).arg(line + 1), id,
                 design::Icon::Code});
        }
        if (quickEditorRows_.size() == QuickSearchNeedle::limits().editorRows)
            quickEditorIncomplete_ = true;
        ++line;
        remainingChars -= length;
        scannedChars += length;
        ++scannedLines;
        // ui-budget: one event-loop pass reads a bounded slice so typing stays responsive.
        if (scannedLines >= 80 || scannedChars >= 16384)
            break;
    }
    renderQuickSearch();
    if (tabIndex < editors_->count() && remainingChars > 0 &&
        quickEditorRows_.size() < QuickSearchNeedle::limits().editorRows)
        QTimer::singleShot(0, this, [this, generation, tabIndex, line, remainingChars] {
            scanQuickSearchEditors(generation, tabIndex, line, remainingChars);
        });
    else {
        if (tabIndex < editors_->count())
            quickEditorIncomplete_ = true;
        quickEditorPending_ = false;
        renderQuickSearch();
    }
}

void MainWindow::refreshQuickSearchIfOpen() {
    if (!quickSearch_ || !quickSearch_->isVisible())
        return;
    if (!quickPendingRecentObject_.isEmpty() && quickSearch_->query().trimmed().isEmpty()) {
        const auto connection =
            quickPendingRecentObject_.value(QStringLiteral("connection")).toULongLong();
        const auto qualifiedName =
            quickPendingRecentObject_.value(QStringLiteral("qualifiedName")).toString();
        // Keep the activated lookup across unrelated UI refreshes. Connection
        // and visibility changes still invalidate it through the normal refresh.
        if (quickSearchConnection() == connection &&
            navigatorController_->model()->canShowUnverifiedObject(connection, qualifiedName)) {
            renderQuickSearch();
            return;
        }
    }
    updateQuickSearch(quickSearch_->query());
}

std::optional<quint64> MainWindow::quickSearchConnection() const {
    if (!navigatorController_)
        return std::nullopt;
    if (browsingConnection_ && navigatorController_->isVisibleConnection(*browsingConnection_))
        return browsingConnection_;
    if (navigatorController_->hasSelectedConnection())
        return navigatorController_->selectedConnection();
    return std::nullopt;
}

void MainWindow::recordQuickObjectVisit(quint64 connection, const QString& objectId) {
    if (objectId.isEmpty())
        return;
    if (!navigatorController_)
        return;
    const auto snapshot = navigatorController_->model()->objectSnapshot(connection, objectId);
    auto* current = qobject_cast<ObjectExplorer*>(editors_->currentWidget());
    if (!snapshot && (!current || current->property("objectId").toString() != objectId))
        return;
    const QString name = snapshot ? snapshot->name : current->property("objectLabel").toString();
    const QString qualifiedName = snapshot ? snapshot->qualifiedName : name;
    const QString kind = snapshot ? snapshot->kind : current->property("objectType").toString();
    const QString parentObjectId = snapshot ? snapshot->parentObjectId : QString{};
    const QVariantList properties = snapshot ? snapshot->properties : QVariantList{};
    quickRecentObjects_.removeIf([&](const QVariantMap& entry) {
        const bool sameObject =
            entry.value(QStringLiteral("connection")).toULongLong() == connection &&
            entry.value(QStringLiteral("objectId")).toString() == objectId &&
            entry.value(QStringLiteral("kind")).toString() == kind;
        const quint64 oldVisit = entry.value(QStringLiteral("visitId")).toULongLong();
        const bool duplicate =
            snapshot ? sameObject : sameObject && quickRecentTabs_.value(oldVisit) == current;
        if (duplicate)
            quickRecentTabs_.remove(oldVisit);
        return duplicate;
    });
    const quint64 visitId = ++nextQuickRecentVisit_;
    if (!snapshot)
        quickRecentTabs_.insert(visitId, current);
    quickRecentObjects_.prepend(
        {{QStringLiteral("connection"), QVariant::fromValue<qulonglong>(connection)},
         {QStringLiteral("visitId"), QVariant::fromValue<qulonglong>(visitId)},
         {QStringLiteral("objectId"), objectId},
         {QStringLiteral("name"), name},
         {QStringLiteral("qualifiedName"), qualifiedName},
         {QStringLiteral("kind"), kind},
         {QStringLiteral("parentObjectId"), parentObjectId},
         {QStringLiteral("properties"), properties},
         {QStringLiteral("targetObjectId"), objectId},
         {QStringLiteral("targetQualifiedName"), qualifiedName},
         {QStringLiteral("targetKind"), kind},
         {QStringLiteral("targetParentObjectId"), parentObjectId},
         {QStringLiteral("targetProperties"), properties},
         {QStringLiteral("targetPane"), -1},
         {QStringLiteral("recent"), true},
         {QStringLiteral("unverified"), !snapshot.has_value()}});
    const auto recentObjectLimit = QuickSearchNeedle::limits().recentObjects;
    if (quickRecentObjects_.size() > recentObjectLimit) {
        quickRecentTabs_.remove(
            quickRecentObjects_.last().value(QStringLiteral("visitId")).toULongLong());
        quickRecentObjects_.resize(recentObjectLimit);
    }
}

void MainWindow::activateQuickSearch(const QString& id) {
    if (!quickSearch_ || !quickSearch_->isVisible())
        return;
    if (recovery_ && (!recovery_->isReady() || recovery_->isClosing())) {
        quickSearch_->setStatus(tr("Wait for workspace recovery or closing to finish."));
        return;
    }
    if (workspace_ && !workspace_->navigationAllowed()) {
        quickSearch_->setStatus(tr("Finish or cancel active database work before navigating."));
        return;
    }
    bool navigated = false;
    if (const auto found = quickObjectMatches_.constFind(id); found != quickObjectMatches_.cend()) {
        const auto object = found.value();
        const quint64 connection = object.value(QStringLiteral("connection")).toULongLong();
        const QString objectId = object.value(QStringLiteral("objectId")).toString();
        const QString kind = object.value(QStringLiteral("kind")).toString();
        const QString qualifiedName = object.value(QStringLiteral("qualifiedName")).toString();
        const QString targetObjectId = object.value(QStringLiteral("targetObjectId")).toString();
        const QString targetKind = object.value(QStringLiteral("targetKind")).toString();
        const QString targetQualifiedName =
            object.value(QStringLiteral("targetQualifiedName")).toString();
        if (object.value(QStringLiteral("recent")).toBool() &&
            object.value(QStringLiteral("unverified")).toBool()) {
            if (quickSearchConnection() != connection || !workspace_ ||
                workspace_->driverForConnection(connection).isEmpty() ||
                !navigatorController_->model()->canShowUnverifiedObject(connection,
                                                                        qualifiedName)) {
                quickSearch_->setStatus(tr("The recent object's connection is unavailable."));
                return;
            }
            auto* tab =
                quickRecentTabs_.value(object.value(QStringLiteral("visitId")).toULongLong())
                    .data();
            if (tab && editors_->indexOf(tab) >= 0 &&
                tab->property("objectConnection").toULongLong() == connection &&
                tab->property("objectId").toString() == objectId &&
                tab->property("objectType").toString() == kind &&
                tab->property("objectLabel").toString() == qualifiedName) {
                if (!allowDocumentChange()) {
                    quickSearch_->setStatus(
                        tr("Finish or cancel active work before switching tabs."));
                    return;
                }
                editors_->setCurrentWidget(tab);
                screens_->setCurrentIndex(static_cast<int>(Screen::Sql));
                tab->setFocus();
                quickSearch_->reject();
                return;
            }
            quickPendingRecentObject_ = object;
            quickRecentStatus_ = tr("Checking the recent object…");
            navigatorController_->startQuickObjectSearch(qualifiedName, connection);
            renderQuickSearch();
            return;
        }
        if (!navigatorController_ || quickSearchConnection() != connection || !workspace_ ||
            workspace_->driverForConnection(connection).isEmpty() ||
            !navigatorController_->model()->matchesObject(
                connection, objectId, kind, qualifiedName,
                object.value(QStringLiteral("parentObjectId")).toString(),
                relationSubtype(object.value(QStringLiteral("properties")).toList()), true) ||
            (targetObjectId != objectId && !targetObjectId.isEmpty() &&
             !navigatorController_->model()->matchesObject(
                 connection, targetObjectId, targetKind, targetQualifiedName,
                 object.value(QStringLiteral("targetParentObjectId")).toString(),
                 relationSubtype(object.value(QStringLiteral("targetProperties")).toList()),
                 true))) {
            updateQuickSearch(quickSearch_->query());
            quickSearch_->setStatus(tr("That object changed. Search results have been refreshed."));
            return;
        }
        if (targetObjectId.isEmpty()) {
            quickSearch_->setStatus(
                tr("This group has no object tab. Choose an object within it."));
            return;
        }
        if (!allowDocumentChange()) {
            quickSearch_->setStatus(tr("Finish or cancel active work before opening an object."));
            return;
        }
        openObjectTab(connection, targetObjectId, targetQualifiedName, targetKind,
                      object.value(QStringLiteral("targetProperties")).toList(),
                      object.value(QStringLiteral("targetPane")).toInt());
        auto* current = qobject_cast<ObjectExplorer*>(editors_->currentWidget());
        if (current && current->property("objectId").toString() == targetObjectId)
            quickSearch_->reject();
        else
            quickSearch_->setStatus(
                tr("The object could not be opened. Retry from the navigator."));
        return;
    }
    if (const auto found = quickHistoryMatches_.constFind(id);
        found != quickHistoryMatches_.cend()) {
        if (!history_ || !history_->isEnabled() || !workspace_ ||
            !workspace_->navigationAllowed() || databaseClosePending_ ||
            (recovery_ && (!recovery_->isReady() || recovery_->isClosing()))) {
            quickSearch_->setStatus(
                tr("History cannot open while the workspace is unavailable or busy."));
            return;
        }
        const auto entry = found.value().value<SavedHistoryEntry>();
        if (!allowDocumentChange()) {
            quickSearch_->setStatus(tr("Finish or cancel active work before opening history."));
            return;
        }
        sidebarHistoryOpen_ = true;
        emit history_->openRequested(entry);
        sidebarHistoryOpen_ = false;
        auto* current = qobject_cast<SqlEditor*>(editors_->currentWidget());
        if (current && current->property("historyRecordId").toString() == entry.id)
            quickSearch_->reject();
        else
            quickSearch_->setStatus(tr("History could not be opened. Retry from Query history."));
        return;
    }
    if (const auto found = quickEditorMatches_.constFind(id); found != quickEditorMatches_.cend()) {
        const auto match = found.value();
        if (!match.editor || editors_->indexOf(match.editor) < 0 ||
            match.editor->revision() != match.revision) {
            updateQuickSearch(quickSearch_->query());
            quickSearch_->setStatus(
                tr("That SQL match changed. Search results have been refreshed."));
            return;
        }
        if (!allowDocumentChange()) {
            quickSearch_->setStatus(tr("Finish or cancel active work before switching tabs."));
            return;
        }
        editors_->setCurrentWidget(match.editor);
        screens_->setCurrentIndex(static_cast<int>(Screen::Sql));
        match.editor->setSelection(match.line, match.column, match.line,
                                   match.column + match.length);
        match.editor->ensureLineVisible(match.line);
        match.editor->setFocus();
        quickSearch_->reject();
        return;
    }
    if (id == QStringLiteral("showStart") && editors_->count()) {
        quickSearch_->setStatus(tr("Close all workspace tabs to return to Start."));
        return;
    }
    if (id == QStringLiteral("showStart"))
        navigated = showScreen(Screen::Start);
    else if (id == QStringLiteral("showSql"))
        navigated = showScreen(Screen::Sql);
    else if (id == QStringLiteral("showObjects"))
        navigated = showScreen(Screen::Object);
    else if (id == QStringLiteral("showHistory"))
        navigated = showScreen(Screen::History);
    else if (id.startsWith(QStringLiteral("tab:"))) {
        const auto target = quickTabTargets_.value(id);
        if (target && editors_->indexOf(target) >= 0 && allowDocumentChange()) {
            editors_->setCurrentWidget(target);
            screens_->setCurrentIndex(static_cast<int>(Screen::Sql));
            target->setFocus();
            navigated = true;
        }
    }
    if (navigated)
        quickSearch_->reject();
    else
        quickSearch_->setStatus(tr("That destination is unavailable or navigation is blocked."));
}

} // namespace choscordb
