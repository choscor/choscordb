#include "widgets/text_prompt_dialog/text_prompt_dialog.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/field/field.h"
#include "design_system/metrics/metrics.h"
#include "design_system/text/text.h"
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace choscordb {
TextPromptDialog::TextPromptDialog(const TextPrompt& prompt, Work work, QWidget* parent)
    : DialogShell(parent), work_(std::move(work)), field_(new QLineEdit(this)) {
    setObjectName(prompt.objectName);
    setWindowTitle(prompt.title);
    setAppModal();
    resize(design::dialogInitialSize(design::DialogSize::Short));
    auto* sections = createSections();
    auto* heading = new design::Text(prompt.title, sections->headerLayout()->parentWidget());
    heading->setTextStyle(design::TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* bodyLayout = sections->bodyLayout();
    auto* description = createDescription(prompt.description, bodyLayout->parentWidget());
    description->setWordWrap(true);
    bodyLayout->addWidget(description);
    field_->setObjectName(prompt.objectName + "Field");
    field_->setText(prompt.text);
    field_->selectAll();
    auto* form = sections->addForm();
    validation_ = new design::FieldValidation(field_, this);
    sections->addFormRow(form, prompt.label, validation_);
    auto* cancel = sections->addAction(design::DialogActionRole::Dismiss, tr("Cancel"));
    cancel->setObjectName(prompt.objectName + "Cancel");
    accept_ = sections->addAction(design::DialogActionRole::Primary, prompt.accept);
    accept_->setObjectName(prompt.objectName + "Accept");
    accept_->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(accept_, &QPushButton::clicked, this, &TextPromptDialog::submit);
    connect(field_, &QLineEdit::textChanged, this, [this] { validation_->setError({}); });
}
void TextPromptDialog::submit() {
    if (busy_)
        return;
    busy_ = true;
    accept_->setEnabled(false);
    const auto text = field_->text();
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, text] {
        const auto error = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        accept_->setEnabled(true);
        if (!error.isEmpty()) {
            validation_->setError(error);
            return;
        }
        emit applied(text);
        accept();
    });
    watcher->setFuture(QtConcurrent::run([work = work_, text] { return work(text); }));
}
}
