#include "widgets/profile_dialog/ssh_private_key_editor.h"
#include "design_system/button/button.h"
#include "design_system/theme.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>
namespace choscordb {
SshPrivateKeyEditor::SshPrivateKeyEditor(const QString& prefix, QWidget* parent) : QWidget(parent) {
    setObjectName(prefix);
    const auto metrics = design::resolveMetrics(design::Density::Compact, true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    preview_ = new QPlainTextEdit(this);
    preview_->setObjectName(prefix + "Preview");
    preview_->setAccessibleName(tr("Private key (contents hidden)"));
    preview_->setReadOnly(true);
    preview_->setUndoRedoEnabled(false);
    preview_->setMaximumHeight(metrics.dataRowHeight * 4);
    preview_->installEventFilter(this);
    layout->addWidget(preview_);
    auto* buttons = new QHBoxLayout;
    auto* pasteButton = new design::Button(tr("Paste private key"), this);
    pasteButton->setObjectName(prefix + "Paste");
    pasteButton->setVariant(design::ButtonVariant::Outline);
    auto* clear = new design::Button(tr("Clear"), this);
    clear->setObjectName(prefix + "Clear");
    clear->setVariant(design::ButtonVariant::Outline);
    buttons->addWidget(pasteButton);
    buttons->addWidget(clear);
    buttons->addStretch();
    layout->addLayout(buttons);
    error_ = new QLabel(this);
    error_->setWordWrap(true);
    layout->addWidget(error_);
    remember_ = new QCheckBox(tr("Save private key in OS credential store"), this);
    remember_->setObjectName(prefix + "Remember");
    layout->addWidget(remember_);
    connect(pasteButton, &QPushButton::clicked, this, &SshPrivateKeyEditor::paste);
    connect(clear, &QPushButton::clicked, this, [this] {
        value_.secret.clear();
        value_.modified = true;
        refresh();
        emit changed();
    });
    connect(remember_, &QCheckBox::toggled, this, &SshPrivateKeyEditor::changed);
    setDraft({});
}
SshPrivateKeyDraft SshPrivateKeyEditor::draft() const {
    auto value = value_;
    value.remember = remember_->isChecked();
    return value;
}
void SshPrivateKeyEditor::setDraft(const SshPrivateKeyDraft& value, bool saved) {
    value_ = value;
    const QSignalBlocker blocker(remember_);
    remember_->setChecked(value.remember);
    preview_->setPlaceholderText(
        saved ? tr("Saved private key — leave unchanged to keep")
              : tr("Paste a complete private key. Its contents stay hidden."));
    refresh();
}
void SshPrivateKeyEditor::refresh() {
    QString masked;
    masked.reserve(value_.secret.size());
    for (const auto ch : value_.secret)
        masked.append(ch == '\n' ? ch : QChar(0x2022));
    preview_->setPlainText(masked);
    error_->clear();
}
void SshPrivateKeyEditor::paste() {
    const auto text = QApplication::clipboard()->text();
    if (text.toUtf8().size() > 65536 || text.contains(QChar::Null)) {
        error_->setText(tr("Private keys must be at most 64 KiB and contain no NUL characters."));
        return;
    }
    value_.secret = text;
    value_.modified = true;
    refresh();
    emit changed();
}
bool SshPrivateKeyEditor::eventFilter(QObject* watched, QEvent* event) {
    if (watched == preview_ && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->matches(QKeySequence::Paste)) {
        paste();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
} // namespace choscordb
