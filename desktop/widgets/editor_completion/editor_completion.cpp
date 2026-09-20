#include "editor_completion.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAbstractItemView>
#include <QCompleter>
#include <QCoreApplication>
#include <QEvent>
#include <QFutureWatcher>
#include <QHash>
#include <QKeyEvent>
#include <QStandardItemModel>
#include <QTimer>
#include <QtConcurrent>
#include <cstdint>
namespace choscordb {
EditorCompletionController::EditorCompletionController(QObject* parent)
    : QObject(parent), completer_(new QCompleter(this)), model_(new QStandardItemModel(this)),
      timer_(new QTimer(this)) {
    completer_->setModel(model_);
    completer_->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    completer_->setMaxVisibleItems(10);
    completer_->popup()->setObjectName("sqlCompletionPopup");
    completer_->popup()->setAccessibleName(tr("SQL suggestions"));
    timer_->setSingleShot(true);
    timer_->setInterval(60);
    connect(timer_, &QTimer::timeout, this, &EditorCompletionController::launch);
    connect(completer_, qOverload<const QModelIndex&>(&QCompleter::activated), this,
            &EditorCompletionController::accept);
}
void EditorCompletionController::dismiss() {
    ++generation_;
    completer_->popup()->hide();
}
void EditorCompletionController::setEditor(SqlEditor* editor) {
    dismiss();
    timer_->stop();
    pending_ = false;
    requested_ = false;
    for (const auto& connection : editorConnections_)
        disconnect(connection);
    editorConnections_.clear();
    if (editor_)
        editor_->removeEventFilter(this);
    editor_ = editor;
    completer_->setWidget(editor);
    if (!editor)
        return;
    editor->setAutoCompletionSource(QsciScintilla::AcsNone);
    editor->installEventFilter(this);
    editorConnections_.append(connect(editor, &SqlEditor::textChanged, this, [this] {
        if (!inserting_)
            requestCompletion(false);
    }));
    editorConnections_.append(connect(editor, &SqlEditor::cursorPositionChanged, this, [this] {
        if (!inserting_)
            dismiss();
    }));
    editorConnections_.append(connect(editor, &QObject::destroyed, this, [this] {
        dismiss();
        timer_->stop();
        pending_ = false;
    }));
}
void EditorCompletionController::setCatalog(CompletionService service) {
    dismiss();
    timer_->stop();
    pending_ = false;
    service_ = std::move(service);
    emit partialCatalog(false);
}
void EditorCompletionController::requestCompletion(bool requested) {
    dismiss();
    requested_ = requested;
    pending_ = true;
    timer_->start(requested ? 0 : 60);
}
bool EditorCompletionController::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress && completer_->popup()->isVisible()) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        const auto key = keyEvent->key();
        if ((key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Tab ||
             key == Qt::Key_Backtab) &&
            (key == Qt::Key_Backtab ||
             (keyEvent->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier)) {
            dismiss();
            timer_->stop();
            pending_ = false;
            if (editor_)
                QCoreApplication::postEvent(editor_, keyEvent->clone());
            event->accept();
            return true;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Tab) {
            accept(completer_->popup()->currentIndex());
            event->accept();
            return true;
        }
        if (key == Qt::Key_Escape) {
            dismiss();
            timer_->stop();
            pending_ = false;
            event->accept();
            return true;
        }
    }
    if (watched == editor_ &&
        (event->type() == QEvent::Hide || event->type() == QEvent::EnabledChange ||
         (event->type() == QEvent::KeyPress &&
          static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape))) {
        dismiss();
        timer_->stop();
        pending_ = false;
    }
    return QObject::eventFilter(watched, event);
}
void EditorCompletionController::launch() {
    if (busy_)
        return;
    pending_ = false;
    if (!editor_ || !editor_->isVisible() || !editor_->isEnabled() || editor_->isReadOnly() ||
        editor_->isIoBusy() || !editor_->hasFocus())
        return;
    const auto cursor = quint64(editor_->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS));
    if (cursor != quint64(editor_->SendScintilla(QsciScintilla::SCI_GETANCHOR)) ||
        quint64(editor_->SendScintilla(QsciScintilla::SCI_GETLENGTH)) >
            CompletionService::limits().maxSourceBytes)
        return;
    const auto source = editor_->text();
    const auto generation = generation_, revision = editor_->revision();
    const QPointer<SqlEditor> target = editor_;
    const auto service = service_;
    const bool requested = requested_;
    busy_ = true;
    auto* watcher = new QFutureWatcher<CompletionPage>(this);
    connect(
        watcher, &QFutureWatcher<CompletionPage>::finished, this,
        [this, watcher, target, generation, revision, cursor] {
            const auto page = watcher->result();
            watcher->deleteLater();
            busy_ = false;
            if (target && target == editor_ && generation == generation_ &&
                revision == target->revision() && target->isEnabled() && target->isVisible() &&
                target->hasFocus() && !target->isReadOnly() &&
                cursor == quint64(target->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS)) &&
                cursor == quint64(target->SendScintilla(QsciScintilla::SCI_GETANCHOR)) &&
                page.valid) {
                emit partialCatalog(page.partial);
                model_->clear();
                QHash<QString, int> names;
                for (const auto& candidate : page.items)
                    ++names[candidate.label];
                for (const auto& candidate : page.items) {
                    const auto& display =
                        names.value(candidate.label) > 1 ? candidate.insertText : candidate.label;
                    auto label = display.left(160);
                    if (!label.isEmpty() && label.back().isHighSurrogate())
                        label.chop(1);
                    if (label.size() < display.size())
                        label += "…";
                    auto* item = new QStandardItem(label + "  ·  " + candidate.kind);
                    item->setData(candidate.insertText, Qt::UserRole);
                    item->setToolTip(candidate.insertText.left(1024));
                    model_->appendRow(item);
                }
                shownGeneration_ = generation;
                shownRevision_ = revision;
                shownStart_ = page.start;
                shownEnd_ = page.end;
                if (model_->rowCount()) {
                    completer_->setCompletionPrefix({});
                    const int x = target->SendScintilla(QsciScintilla::SCI_POINTXFROMPOSITION, 0,
                                                        long(cursor));
                    const int y = target->SendScintilla(QsciScintilla::SCI_POINTYFROMPOSITION, 0,
                                                        long(cursor));
                    const int line =
                        target->SendScintilla(QsciScintilla::SCI_LINEFROMPOSITION, long(cursor));
                    const int height = target->SendScintilla(QsciScintilla::SCI_TEXTHEIGHT, line);
                    completer_->complete(QRect(x, y, 420, height));
                    completer_->popup()->installEventFilter(this);
                    completer_->popup()->setCurrentIndex(
                        completer_->completionModel()->index(0, 0));
                }
            }
            if (pending_)
                timer_->start(0);
        });
    watcher->setFuture(QtConcurrent::run([service, source, cursor, requested] {
        return service.complete(source, cursor, requested);
    }));
}
void EditorCompletionController::accept(const QModelIndex& index) {
    if (!editor_ || shownGeneration_ != generation_ || shownRevision_ != editor_->revision() ||
        !editor_->isEnabled() || editor_->isReadOnly() || editor_->isIoBusy() ||
        shownEnd_ != quint64(editor_->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS)) ||
        shownEnd_ != quint64(editor_->SendScintilla(QsciScintilla::SCI_GETANCHOR)))
        return;
    const auto insertion = index.data(Qt::UserRole).toString().toUtf8();
    const auto length = quint64(editor_->SendScintilla(QsciScintilla::SCI_GETLENGTH));
    if (shownStart_ > shownEnd_ || shownEnd_ > length ||
        quint64(insertion.size()) >
            CompletionService::limits().maxSourceBytes - (length - (shownEnd_ - shownStart_)))
        return;
    inserting_ = true;
    editor_->beginUndoAction();
    editor_->SendScintilla(QsciScintilla::SCI_SETTARGETSTART, long(shownStart_));
    editor_->SendScintilla(QsciScintilla::SCI_SETTARGETEND, long(shownEnd_));
    editor_->SendScintilla(QsciScintilla::SCI_REPLACETARGET,
                           static_cast<std::uintptr_t>(insertion.size()), insertion.constData());
    editor_->SendScintilla(QsciScintilla::SCI_GOTOPOS, long(shownStart_ + insertion.size()));
    editor_->endUndoAction();
    inserting_ = false;
    dismiss();
}
} // namespace choscordb
