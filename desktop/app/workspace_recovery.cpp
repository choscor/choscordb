#include "app/workspace_recovery.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QSet>
#include <QTabBar>
#include <QTabWidget>
#include <QUuid>
#include <atomic>
namespace choscordb {
namespace {
quint64 nextToken() {
    static std::atomic<quint64> token{quint64(1) << 62};
    return token.fetch_add(1);
}
} // namespace
WorkspaceRecoveryController::WorkspaceRecoveryController(QTabWidget* tabs,
                                                         std::function<SqlEditor*()> addEditor,
                                                         QObject* parent)
    : QObject(parent), tabs_(tabs), addEditor_(std::move(addEditor)) {
    qRegisterMetaType<QList<SavedEditorDocument>>();
    debounce_.setSingleShot(true);
    debounce_.setInterval(350);
    connect(&debounce_, &QTimer::timeout, this, &WorkspaceRecoveryController::flush);
    connect(tabs_->tabBar(), &QTabBar::tabMoved, this, &WorkspaceRecoveryController::changed);
    for (int i = 0; i < tabs_->count(); ++i)
        watchEditor(qobject_cast<SqlEditor*>(tabs_->widget(i)));
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
    pending_ = nextToken();
    emit restoreRequested(pending_);
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
QList<SavedEditorDocument> WorkspaceRecoveryController::snapshot() const {
    QList<SavedEditorDocument> documents;
    for (int i = 0; i < tabs_->count(); ++i) {
        auto* editor = qobject_cast<SqlEditor*>(tabs_->widget(i));
        if (!editor)
            continue;
        SavedEditorDocument document;
        document.id = editor->property("documentId").toString();
        document.title = tabs_->tabText(i);
        if (document.title.endsWith(QStringLiteral(" •")))
            document.title.chop(2);
        document.sql = editor->text();
        document.filePath = editor->filePath();
        document.profileId = editor->property("profileId").toString();
        document.modified = editor->isModified();
        document.cursorOffset =
            static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS));
        document.selectionAnchor =
            static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETANCHOR));
        documents.append(std::move(document));
    }
    return documents;
}
void WorkspaceRecoveryController::apply(const QList<SavedEditorDocument>& documents) {
    applying_ = true;
    while (tabs_->count()) {
        auto* old = tabs_->widget(0);
        tabs_->removeTab(0);
        old->deleteLater();
    }
    for (const auto& document : documents) {
        auto* editor = addEditor_();
        watchEditor(editor);
        editor->setProperty("documentId", document.id);
        editor->setProfileId(document.profileId);
        editor->setProperty("documentTitle", document.title);
        editor->restoreDocument(document.sql.toUtf8(), document.filePath, document.cursorOffset,
                                document.selectionAnchor, document.modified);
        tabs_->setTabText(tabs_->indexOf(editor),
                          document.title + (document.modified ? QStringLiteral(" •") : QString()));
    }
    if (documents.isEmpty())
        watchEditor(addEditor_());
    tabs_->setCurrentIndex(0);
    applying_ = false;
}
void WorkspaceRecoveryController::restored(quint64 token,
                                           const QList<SavedEditorDocument>& documents) {
    if (token != pending_ || !restoring_)
        return;
    // Validate every buffer before replacing any existing tab. The storage and
    // bridge validate too; this protects injected or malformed native responses.
    const auto limits = EngineAdapter::recoveryLimits();
    quint64 total = 0;
    QSet<QString> ids;
    if (static_cast<quint64>(documents.size()) > limits.maxDocuments) {
        failed(token, tr("Saved workspace has too many tabs."));
        return;
    }
    for (const auto& document : documents) {
        const auto bytes = document.sql.toUtf8();
        total += static_cast<quint64>(bytes.size());
        if (static_cast<quint64>(bytes.size()) > limits.maxSqlBytes ||
            bytes.size() > DocumentIo::MaximumBytes || total > limits.maxCollectionBytes ||
            ids.contains(document.id)) {
            failed(token, tr("Saved workspace data exceeds limits or has duplicate document IDs."));
            return;
        }
        ids.insert(document.id);
        auto boundary = [&bytes](quint64 offset) {
            return offset <= static_cast<quint64>(bytes.size()) &&
                   (offset == static_cast<quint64>(bytes.size()) ||
                    (static_cast<unsigned char>(bytes.at(static_cast<qsizetype>(offset))) & 0xc0) !=
                        0x80);
        };
        if (document.id.isEmpty() || !boundary(document.cursorOffset) ||
            !boundary(document.selectionAnchor)) {
            failed(token, tr("Saved workspace data is invalid."));
            return;
        }
    }
    pending_ = 0;
    restoring_ = false;
    failed_ = false;
    apply(documents);
    ready_ = true;
    dirty_ = false;
    setEnabled(!closing_);
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
    pending_ = nextToken();
    sentRevision_ = revision_;
    emit saveRequested(snapshot(), pending_);
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
    apply({});
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
