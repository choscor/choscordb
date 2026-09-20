#include "search_panel.h"
#include "bridge/engine_adapter.h"
#include "design_system/button/button.h"
#include "design_system/button_group/button_group.h"
#include "design_system/field/field.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include "design_system/toast_region/toast_region.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QCheckBox>
#include <QFutureWatcher>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cstdint>
namespace choscordb {
SearchPanel::SearchPanel(std::function<SqlEditor*()> currentEditor, QWidget* parent)
    : QWidget(parent), currentEditor_(std::move(currentEditor)) {
    setObjectName("searchPanel");
    setAccessibleName(tr("Search and replace"));
    const auto metrics = design::resolveMetrics(design::Density::Compact, true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(metrics.spacingMedium, metrics.spacingSmall, metrics.spacingMedium,
                               metrics.spacingSmall);
    layout->setSpacing(metrics.spacingMedium);
    auto* first = new QHBoxLayout;
    first->setSpacing(metrics.spacingMedium);
    needle_ = new QLineEdit(this);
    needle_->setObjectName("searchNeedle");
    needle_->setAccessibleName(tr("Find text"));
    needle_->setMaxLength(16 * 1024 + 1);
    needle_->setPlaceholderText(tr("Find text"));
    auto* previous = new design::Button(tr("Previous"), this);
    auto* next = new design::Button(tr("Next"), this);
    next->setObjectName("searchNext");
    previous->setObjectName("searchPrevious");
    previous->setVariant(design::ButtonVariant::Outline);
    next->setVariant(design::ButtonVariant::Outline);
    previous->setDesignIcon(design::Icon::ChevronLeft);
    next->setDesignIcon(design::Icon::ChevronRight);
    previous->setButtonSize(design::ButtonSize::Small);
    next->setButtonSize(design::ButtonSize::Small);
    auto* navigation = new design::ButtonGroup(Qt::Horizontal, this);
    navigation->setObjectName("searchNavigation");
    navigation->addButton(previous);
    navigation->addButton(next);
    case_ = new QCheckBox(tr("Match case"), this);
    case_->setObjectName("searchCase");
    word_ = new QCheckBox(tr("Whole word"), this);
    word_->setObjectName("searchWord");
    auto* close = new design::Button(tr("Close"), this);
    close->setObjectName("searchClose");
    close->setVariant(design::ButtonVariant::Ghost);
    close->setButtonSize(design::ButtonSize::Small);
    close->setDesignIcon(design::Icon::Close);
    needleValidation_ = new design::FieldValidation(needle_, this);
    first->addWidget(needleValidation_, 1);
    first->addWidget(navigation);
    first->addWidget(close);
    layout->addLayout(first);
    auto* options = new QHBoxLayout;
    options->setSpacing(metrics.spacingLarge);
    options->addWidget(case_);
    options->addWidget(word_);
    options->addStretch();
    layout->addLayout(options);
    replacementRow_ = new QWidget(this);
    auto* second = new QHBoxLayout(replacementRow_);
    second->setSpacing(metrics.spacingMedium);
    second->setContentsMargins(0, 0, 0, 0);
    replacement_ = new QLineEdit(this);
    replacement_->setObjectName("searchReplacement");
    replacement_->setAccessibleName(tr("Replacement text"));
    replacement_->setMaxLength(16 * 1024 * 1024 + 1);
    replacement_->setPlaceholderText(tr("Replace with"));
    auto* replace = new design::Button(tr("Replace"), this);
    replace->setObjectName("searchReplace");
    replaceAll_ = new design::Button(tr("Replace all"), this);
    replaceAll_->setObjectName("searchReplaceAll");
    replace->setVariant(design::ButtonVariant::Outline);
    replace->setButtonSize(design::ButtonSize::Small);
    replaceAll_->setButtonSize(design::ButtonSize::Small);
    replacementValidation_ = new design::FieldValidation(replacement_, this);
    second->addWidget(replacementValidation_, 1);
    second->addWidget(replace);
    second->addWidget(replaceAll_);
    layout->addWidget(replacementRow_);
    status_ = new design::Text({}, this);
    status_->setObjectName("searchStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    connect(next, &QPushButton::clicked, this, &SearchPanel::findNext);
    connect(previous, &QPushButton::clicked, this, &SearchPanel::findPrevious);
    connect(needle_, &QLineEdit::returnPressed, this, &SearchPanel::findNext);
    connect(replace, &QPushButton::clicked, this, &SearchPanel::replaceOne);
    connect(replaceAll_, &QPushButton::clicked, this, &SearchPanel::replaceAll);
    connect(close, &QPushButton::clicked, this, [this] {
        hide();
        if (auto* e = currentEditor_())
            e->setFocus();
    });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, close, &QPushButton::click);
    connect(needle_, &QLineEdit::textChanged, this, [this] {
        needleValidation_->setError({});
        invalidate();
    });
    connect(replacement_, &QLineEdit::textChanged, this, [this] {
        replacementValidation_->setError({});
        ++generation_;
    });
    connect(case_, &QCheckBox::toggled, this, [this] { invalidate(); });
    connect(word_, &QCheckBox::toggled, this, [this] { invalidate(); });
    hide();
}
void SearchPanel::showFind() {
    if (auto* editor = editable(false)) {
        const auto start = editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONSTART);
        const auto end = editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONEND);
        if (end > start && end - start <= 16 * 1024)
            needle_->setText(editor->selectedText());
    }
    show();
    replacementRow_->hide();
    needle_->setFocus();
}
void SearchPanel::showReplace() {
    showFind();
    replacementRow_->show();
}
void SearchPanel::findNext() {
    find(false);
}
void SearchPanel::findPrevious() {
    find(true);
}
void SearchPanel::editorChanged() {
    invalidate();
}
void SearchPanel::hideEvent(QHideEvent* event) {
    invalidate();
    QWidget::hideEvent(event);
}
void SearchPanel::invalidate() {
    hasMatch_ = false;
    ++generation_;
}
SqlEditor* SearchPanel::editable(bool mutation) const {
    auto* editor = currentEditor_();
    return editor && editor->isEnabled() && (!mutation || !editor->isReadOnly()) &&
                   !editor->isIoBusy()
               ? editor
               : nullptr;
}
void SearchPanel::setPending(bool pending) {
    if (pending)
        progressToast(this)->showProgress(tr("Search"), tr("Working…"));
    else
        clearProgressToast(this);
    if (pending)
        status_->clear();
    pending_ = pending;
    for (auto* button : findChildren<QPushButton*>())
        if (button->objectName() != "searchClose")
            button->setEnabled(!pending);
}
void SearchPanel::find(bool backwards) {
    if (isHidden()) {
        show();
        replacementRow_->hide();
    }
    auto* editor = editable(false);
    if (!editor) {
        status_->setText(tr("The editor is unavailable."));
        return;
    }
    if (pending_) {
        status_->setText(tr("A search operation is still running."));
        return;
    }
    if (editor->SendScintilla(QsciScintilla::SCI_GETLENGTH) > 16 * 1024 * 1024) {
        status_->setText(tr("Search supports documents up to 16 MiB."));
        return;
    }
    const quint64 cursor = editor->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS);
    const quint64 anchor = editor->SendScintilla(QsciScintilla::SCI_GETANCHOR);
    const quint64 revision = editor->revision();
    const quint64 request = ++generation_;
    const quint64 start = backwards ? std::min(cursor, anchor) : std::max(cursor, anchor);
    QPointer<SqlEditor> target(editor);
    const auto source = editor->text();
    const auto needle = needle_->text();
    const bool caseSensitive = case_->isChecked(), wholeWord = word_->isChecked();
    hasMatch_ = false;
    setPending(true);
    auto* watcher = new QFutureWatcher<TextMatch>(this);
    connect(watcher, &QFutureWatcher<TextMatch>::finished, this,
            [this, watcher, target, revision, request, cursor, anchor] {
                const auto result = watcher->result();
                watcher->deleteLater();
                setPending(false);
                if (!target || editable(false) != target || target->revision() != revision ||
                    request != generation_ || !isVisible() ||
                    quint64(target->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS)) != cursor ||
                    quint64(target->SendScintilla(QsciScintilla::SCI_GETANCHOR)) != anchor) {
                    status_->setText(tr("Search discarded because the editor or search changed."));
                    return;
                }
                if (!result.valid) {
                    needleValidation_->setError(result.error);
                    return;
                }
                if (!result.found) {
                    status_->setText(tr("No match found."));
                    return;
                }
                target->SendScintilla(QsciScintilla::SCI_SETSEL,
                                      static_cast<unsigned long>(result.start),
                                      static_cast<long>(result.end));
                matchedEditor_ = target;
                matchRevision_ = revision;
                matchStart_ = result.start;
                matchEnd_ = result.end;
                hasMatch_ = true;
                status_->setText(result.wrapped ? tr("Search wrapped around the document.")
                                                : tr("Match found."));
            });
    watcher->setFuture(QtConcurrent::run([source, needle, start, backwards, caseSensitive,
                                          wholeWord] {
        return EngineAdapter::findText(source, needle, start, backwards, caseSensitive, wholeWord);
    }));
}
void SearchPanel::replaceOne() {
    auto* editor = editable();
    if (!editor) {
        status_->setText(tr("The editor cannot be changed."));
        return;
    }
    if (pending_)
        return;
    if (!hasMatch_ || matchedEditor_ != editor || editor->revision() != matchRevision_ ||
        quint64(editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONSTART)) != matchStart_ ||
        quint64(editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONEND)) != matchEnd_) {
        find(false);
        return;
    }
    const auto length = editor->SendScintilla(QsciScintilla::SCI_GETLENGTH);
    if (length > 16 * 1024 * 1024 || replacement_->text().size() > 16 * 1024 * 1024) {
        replacementValidation_->setError(tr("Replacement output exceeds 16 MiB."));
        return;
    }
    if (!replacement_->text().isValidUtf16()) {
        replacementValidation_->setError(tr("Replacement input is not valid Unicode."));
        return;
    }
    const auto bytes = replacement_->text().toUtf8();
    if (quint64(length) - (matchEnd_ - matchStart_) + quint64(bytes.size()) > 16 * 1024 * 1024) {
        replacementValidation_->setError(tr("Replacement output exceeds 16 MiB."));
        return;
    }
    const auto start = matchStart_;
    editor->SendScintilla(QsciScintilla::SCI_BEGINUNDOACTION);
    editor->SendScintilla(QsciScintilla::SCI_SETTARGETSTART,
                          static_cast<unsigned long>(matchStart_));
    editor->SendScintilla(QsciScintilla::SCI_SETTARGETEND, static_cast<unsigned long>(matchEnd_));
    editor->SendScintilla(QsciScintilla::SCI_REPLACETARGET,
                          static_cast<std::uintptr_t>(bytes.size()), bytes.constData());
    editor->SendScintilla(QsciScintilla::SCI_ENDUNDOACTION);
    editor->SendScintilla(QsciScintilla::SCI_SETSEL, static_cast<unsigned long>(start),
                          static_cast<long>(start + bytes.size()));
    invalidate();
    status_->setText(tr("Replaced one match."));
}
void SearchPanel::replaceAll() {
    auto* editor = editable();
    if (!editor) {
        status_->setText(tr("The editor cannot be changed."));
        return;
    }
    if (pending_)
        return;
    if (editor->SendScintilla(QsciScintilla::SCI_GETLENGTH) > 16 * 1024 * 1024) {
        status_->setText(tr("Search supports documents up to 16 MiB."));
        return;
    }
    const quint64 revision = editor->revision(), request = ++generation_;
    QPointer<SqlEditor> target(editor);
    const auto source = editor->text(), needle = needle_->text(),
               replacement = replacement_->text();
    if (needle.isEmpty() || !needle.isValidUtf16()) {
        needleValidation_->setError(needle.isEmpty() ? tr("Enter text to find.")
                                                     : tr("Search text is not valid Unicode."));
        return;
    }
    if (!replacement.isValidUtf16()) {
        replacementValidation_->setError(tr("Replacement input is not valid Unicode."));
        return;
    }
    const bool caseSensitive = case_->isChecked(), wholeWord = word_->isChecked();
    hasMatch_ = false;
    setPending(true);
    auto* watcher = new QFutureWatcher<TextReplacement>(this);
    connect(
        watcher, &QFutureWatcher<TextReplacement>::finished, this,
        [this, watcher, target, revision, request] {
            const auto result = watcher->result();
            watcher->deleteLater();
            setPending(false);
            if (!target || editable() != target || target->revision() != revision ||
                request != generation_ || !isVisible()) {
                status_->setText(tr("Replacement discarded because the editor or search changed."));
                return;
            }
            if (!result.valid) {
                status_->setText(result.error);
                return;
            }
            if (result.count != 0) {
                const auto bytes = result.text.toUtf8();
                if (bytes.size() > 16 * 1024 * 1024) {
                    replacementValidation_->setError(tr("Replacement output exceeds 16 MiB."));
                    return;
                }
                target->SendScintilla(QsciScintilla::SCI_BEGINUNDOACTION);
                target->SendScintilla(QsciScintilla::SCI_SETTARGETSTART, 0UL);
                target->SendScintilla(QsciScintilla::SCI_SETTARGETEND,
                                      static_cast<unsigned long>(
                                          target->SendScintilla(QsciScintilla::SCI_GETLENGTH)));
                target->SendScintilla(QsciScintilla::SCI_REPLACETARGET,
                                      static_cast<std::uintptr_t>(bytes.size()), bytes.constData());
                target->SendScintilla(QsciScintilla::SCI_ENDUNDOACTION);
            }
            invalidate();
            status_->setText(tr("Replaced %1 matches.").arg(result.count));
        });
    watcher->setFuture(QtConcurrent::run([source, needle, replacement, caseSensitive, wholeWord] {
        return EngineAdapter::replaceAllText(source, needle, replacement, caseSensitive, wholeWord);
    }));
}
} // namespace choscordb
