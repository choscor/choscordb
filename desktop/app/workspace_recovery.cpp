#include "app/workspace_recovery.h"
#include "app/object_explorer.h"
#include "app/object_kind_icon.h"
#include "app/object_tab_title.h"
#include "bridge/request_token.h"
#include "design_system/icons.h"
#include "design_system/theme.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QSet>
#include <QTabBar>
#include <QTabWidget>
#include <QUuid>
namespace choscordb {
quint32 savedObjectPane(int paneIndex) {
    if (paneIndex == 4) // ERD is inserted before Data in the UI.
        return 5;
    if (paneIndex == 5)
        return 4;
    return static_cast<quint32>(paneIndex);
}
int restoredObjectPane(quint32 savedPane) {
    if (savedPane == 4) // Legacy value 4 must continue to select Data.
        return 5;
    if (savedPane == 5)
        return 4;
    return static_cast<int>(savedPane);
}
WorkspaceRecoveryController::WorkspaceRecoveryController(QTabWidget* tabs,
                                                         std::function<SqlEditor*()> addEditor,
                                                         QObject* parent)
    : QObject(parent), tabs_(tabs), addEditor_(std::move(addEditor)) {
    qRegisterMetaType<QList<SavedWorkspaceTab>>();
    debounce_.setSingleShot(true);
    debounce_.setInterval(350);
    connect(&debounce_, &QTimer::timeout, this, &WorkspaceRecoveryController::flush);
    connect(tabs_->tabBar(), &QTabBar::tabMoved, this, &WorkspaceRecoveryController::changed);
    connect(tabs_, &QTabWidget::currentChanged, this, &WorkspaceRecoveryController::changed);
    for (int i = 0; i < tabs_->count(); ++i)
        watchEditor(qobject_cast<SqlEditor*>(tabs_->widget(i)));
}
void WorkspaceRecoveryController::setObjectFactory(
    std::function<QWidget*(const SavedWorkspaceTab&)> factory) {
    objectFactory_ = std::move(factory);
}
void WorkspaceRecoveryController::setEnabled(bool enabled) {
    tabs_->setEnabled(enabled);
    emit mutationEnabled(enabled);
}
void WorkspaceRecoveryController::start() {
    if (pending_ || ready_)
        return;
    beginRestore();
}
void WorkspaceRecoveryController::beginRestore() {
    failed_ = false;
    restoring_ = true;
    setEnabled(false);
    pending_ = nextRequestToken();
    emit restoreTabsRequested(pending_);
}
void WorkspaceRecoveryController::watchEditor(SqlEditor* editor) {
    if (!editor || editor->property("recoveryWatched").toBool())
        return;
    editor->setProperty("recoveryWatched", true);
    if (editor->property("documentId").toString().isEmpty())
        editor->setProperty("documentId", QUuid::createUuid().toString(QUuid::WithoutBraces));
    connect(editor, &SqlEditor::profileAssociationChanged, this,
            &WorkspaceRecoveryController::changed);
    connect(editor, &SqlEditor::textChanged, this, &WorkspaceRecoveryController::changed);
    connect(editor, &SqlEditor::cursorPositionChanged, this, &WorkspaceRecoveryController::changed);
    connect(editor, &SqlEditor::selectionChanged, this, &WorkspaceRecoveryController::changed);
    connect(editor, &SqlEditor::modificationChanged, this, &WorkspaceRecoveryController::changed);
    const auto fileFinished = [this](const QString&, const QString& error) {
        if (error.isEmpty())
            changed();
        // A pending file operation defers snapshots, including close flushes.
        // Both success and failure settle the operation and permit a retry.
        if (closing_)
            flush();
        else if (dirty_ && !failed_)
            debounce_.start();
    };
    connect(editor, &SqlEditor::fileOpened, this, fileFinished);
    connect(editor, &SqlEditor::fileSaved, this, fileFinished);
}
QList<SavedWorkspaceTab> WorkspaceRecoveryController::snapshotTabs() const {
    QList<SavedWorkspaceTab> tabs;
    for (int i = 0; i < tabs_->count(); ++i) {
        auto* widget = tabs_->widget(i);
        SavedWorkspaceTab tab;
        if (auto* editor = qobject_cast<SqlEditor*>(widget)) {
            tab.document.id = editor->property("documentId").toString();
            tab.document.title = tabs_->tabText(i);
            if (tab.document.title.endsWith(QStringLiteral(" •")))
                tab.document.title.chop(2);
            tab.document.sql = editor->text();
            tab.document.filePath = editor->filePath();
            tab.document.profileId = editor->property("profileId").toString();
            tab.document.modified = editor->isModified();
            tab.document.cursorOffset =
                static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS));
            tab.document.selectionAnchor =
                static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETANCHOR));
        } else if (auto* object = qobject_cast<ObjectExplorer*>(widget)) {
            tab.isObject = true;
            tab.profileId = object->property("objectProfileId").toString();
            tab.objectType = object->property("objectType").toString();
            tab.objectId = object->property("objectId").toString();
            tab.label = object->property("objectLabel").toString();
            tab.pane = savedObjectPane(object->paneIndex());
        } else
            continue;
        tabs.append(std::move(tab));
    }
    return tabs;
}
void WorkspaceRecoveryController::clearTabs() {
    while (tabs_->count()) {
        auto* old = tabs_->widget(0);
        tabs_->removeTab(0);
        old->deleteLater();
    }
}
void WorkspaceRecoveryController::applyTabs(const QList<SavedWorkspaceTab>& tabs,
                                            quint32 activeIndex) {
    applying_ = true;
    clearTabs();
    for (const auto& tab : tabs) {
        if (tab.isObject) {
            auto* widget = objectFactory_ ? objectFactory_(tab) : nullptr;
            if (widget && tabs_->indexOf(widget) < 0) {
                const auto icon = design::themedIcon(
                    objectKindIcon(tab.objectType),
                    design::resolvedThemeForWidget(*tabs_).colors.mutedText, objectIconSize());
                tabs_->addTab(widget, icon, objectTabTitle(tab.objectId, tab.label));
            }
        } else {
            const auto& document = tab.document;
            auto* editor = addEditor_();
            watchEditor(editor);
            editor->setProperty("documentId", document.id);
            editor->setProfileId(document.profileId);
            editor->setProperty("documentTitle", document.title);
            editor->restoreDocument(document.sql.toUtf8(), document.filePath, document.cursorOffset,
                                    document.selectionAnchor, document.modified);
            tabs_->setTabText(tabs_->indexOf(editor),
                              document.title +
                                  (document.modified ? QStringLiteral(" •") : QString()));
        }
    }
    if (!tabs.isEmpty())
        tabs_->setCurrentIndex(static_cast<int>(activeIndex));
    applying_ = false;
}
void WorkspaceRecoveryController::restoredTabs(quint64 token, const QList<SavedWorkspaceTab>& tabs,
                                               quint32 activeIndex) {
    if (token != pending_ || !restoring_)
        return;
    const auto limits = EngineAdapter::recoveryLimits();
    if (static_cast<quint64>(tabs.size()) > limits.maxDocuments ||
        (tabs.isEmpty() ? activeIndex != 0 : activeIndex >= static_cast<quint32>(tabs.size()))) {
        failed(token, tr("Saved workspace tab order is invalid."));
        return;
    }
    QSet<QString> identities;
    quint64 total = 0;
    for (const auto& tab : tabs) {
        if (tab.isObject) {
            const auto key =
                QStringLiteral("object:%1:%2:%3").arg(tab.profileId, tab.objectType, tab.objectId);
            if (tab.profileId.isEmpty() || tab.objectType.isEmpty() || tab.objectId.isEmpty() ||
                tab.label.isEmpty() || tab.pane > 5 ||
                (tab.pane == 5 && tab.objectType != QStringLiteral("table")) ||
                identities.contains(key)) {
                failed(token, tr("Saved object tab is invalid."));
                return;
            }
            identities.insert(key);
            total += static_cast<quint64>(tab.profileId.toUtf8().size() +
                                          tab.objectType.toUtf8().size() +
                                          tab.objectId.toUtf8().size() + tab.label.toUtf8().size());
        } else {
            const auto& d = tab.document;
            const auto bytes = d.sql.toUtf8();
            const auto key = QStringLiteral("sql:%1").arg(d.id);
            auto boundary = [&bytes](quint64 offset) {
                return offset <= static_cast<quint64>(bytes.size()) &&
                       (offset == static_cast<quint64>(bytes.size()) ||
                        (static_cast<unsigned char>(bytes.at(static_cast<qsizetype>(offset))) &
                         0xc0) != 0x80);
            };
            if (d.id.isEmpty() || identities.contains(key) ||
                static_cast<quint64>(bytes.size()) > limits.maxSqlBytes ||
                !boundary(d.cursorOffset) || !boundary(d.selectionAnchor)) {
                failed(token, tr("Saved SQL tab is invalid."));
                return;
            }
            identities.insert(key);
            total += static_cast<quint64>(bytes.size());
        }
        if (total > limits.maxCollectionBytes) {
            failed(token, tr("Saved workspace exceeds recovery limits."));
            return;
        }
    }
    pending_ = 0;
    restoring_ = false;
    failed_ = false;
    applyTabs(tabs, activeIndex);
    ready_ = true;
    dirty_ = false;
    setEnabled(!closing_);
    emit restoreCompleted(!tabs.isEmpty());
    emit persistenceSucceeded();
    if (closing_)
        flush();
}
void WorkspaceRecoveryController::changed() {
    if (!ready_ || applying_)
        return;
    ++revision_;
    dirty_ = true;
    if (!failed_ && !closing_)
        debounce_.start();
}
void WorkspaceRecoveryController::flush() {
    debounce_.stop();
    if (!ready_ || restoring_ || pending_ || failed_)
        return;
    if (!dirty_ && !closing_)
        return;
    const auto limits = EngineAdapter::recoveryLimits();
    quint64 total = 0;
    QString error;
    if (static_cast<quint64>(tabs_->count()) > limits.maxDocuments)
        error = tr("Workspace has too many tabs to save for recovery.");
    for (int i = 0; error.isEmpty() && i < tabs_->count(); ++i) {
        auto* editor = qobject_cast<SqlEditor*>(tabs_->widget(i));
        if (!editor)
            continue;
        if (editor->isIoBusy())
            return;
        const auto bytes =
            static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETLENGTH));
        total += bytes;
        if (bytes > limits.maxSqlBytes || total > limits.maxCollectionBytes)
            error = tr("Workspace is too large to save for recovery.");
    }
    if (!error.isEmpty()) {
        failed_ = true;
        emit errorOccurred(error, closing_);
        return;
    }
    pending_ = nextRequestToken();
    sentRevision_ = revision_;
    emit saveTabsRequested(snapshotTabs(), static_cast<quint32>(qMax(0, tabs_->currentIndex())),
                           pending_);
}
void WorkspaceRecoveryController::saved(quint64 token) {
    if (token != pending_ || restoring_)
        return;
    pending_ = 0;
    failed_ = false;
    dirty_ = revision_ != sentRevision_;
    emit persistenceSucceeded();
    if (dirty_) {
        QTimer::singleShot(0, this, &WorkspaceRecoveryController::flush);
        return;
    }
    if (closing_)
        QTimer::singleShot(0, this, [this] {
            if (closing_ && !pending_ && !dirty_)
                emit closeReady();
        });
}
void WorkspaceRecoveryController::failed(quint64 token, const QString& error) {
    if (!pending_ || token != pending_)
        return;
    pending_ = 0;
    failed_ = true;
    debounce_.stop();
    emit errorOccurred(error, closing_);
}
void WorkspaceRecoveryController::retry() {
    if (pending_)
        return;
    failed_ = false;
    if (!ready_)
        beginRestore();
    else {
        dirty_ = true;
        flush();
    }
}
void WorkspaceRecoveryController::startEmpty() {
    if (pending_ || ready_)
        return;
    restoring_ = false;
    failed_ = false;
    applying_ = true;
    clearTabs();
    watchEditor(addEditor_());
    tabs_->setCurrentIndex(0);
    applying_ = false;
    ready_ = true;
    setEnabled(!closing_);
    changed();
    if (closing_)
        flush();
}
void WorkspaceRecoveryController::requestClose() {
    if (closing_)
        return;
    closing_ = true;
    setEnabled(false);
    if (pending_)
        return;
    if (!ready_) {
        emit errorOccurred(
            tr("Recovery has not completed. Retry recovery or close without saving."), true);
        return;
    }
    failed_ = false;
    flush();
}
void WorkspaceRecoveryController::cancelClose() {
    closing_ = false;
    setEnabled(ready_);
}
void WorkspaceRecoveryController::closeWithoutRecovery() {
    closing_ = true;
    debounce_.stop();
    emit closeReady();
}
} // namespace choscordb
