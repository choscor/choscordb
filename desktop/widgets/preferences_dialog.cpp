#include "preferences_dialog.h"
#include "widgets/sql_editor.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <atomic>
namespace choscordb {
namespace {
quint64 nextToken() {
    static std::atomic<quint64> token{quint64(1) << 57};
    return token.fetch_add(1);
}
} // namespace
PreferencesDialog::PreferencesDialog(EngineAdapter* adapter, QList<ShortcutDescriptor> catalog,
                                     QWidget* parent)
    : QDialog(parent), adapter_(adapter), catalog_(std::move(catalog)) {
    setWindowTitle(tr("Preferences"));
    setObjectName("preferencesDialog");
    resize(620, 520);
    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    pages_ = tabs;
    layout->addWidget(tabs);
    auto* editor = new QWidget(tabs);
    auto* form = new QFormLayout(editor);
    system_ = new QCheckBox(tr("Use system monospace font"), editor);
    system_->setObjectName("preferencesSystemFont");
    font_ = new QFontComboBox(editor);
    font_->setObjectName("preferencesFont");
    size_ = new QSpinBox(editor);
    size_->setObjectName("preferencesFontSize");
    const auto limits = EngineAdapter::editorPreferenceLimits();
    size_->setRange(limits.minFontSize, limits.maxFontSize);
    form->addRow(system_);
    form->addRow(tr("Font family"), font_);
    form->addRow(tr("Font size"), size_);
    preview_ = new SqlEditor(editor);
    preview_->setObjectName("preferencesPreview");
    preview_->setText("SELECT name, count(*)\nFROM sample\nWHERE active = true\nGROUP BY name;");
    preview_->setReadOnly(true);
    form->addRow(preview_);
    tabs->addTab(editor, tr("Editor"));
    auto* scroll = new QScrollArea(tabs);
    scroll->setWidgetResizable(true);
    auto* keyboard = new QWidget(scroll);
    auto* keys = new QFormLayout(keyboard);
    for (const auto& descriptor : catalog_) {
        auto* key = new QKeySequenceEdit(keyboard);
        key->setObjectName("shortcut_" + descriptor.id);
        key->setEnabled(descriptor.configurable);
        sequences_.append(key);
        keys->addRow(descriptor.label, key);
    }
    scroll->setWidget(keyboard);
    tabs->addTab(scroll, tr("Keyboard"));
    status_ = new QLabel(this);
    status_->setObjectName("preferencesStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel |
                                             QDialogButtonBox::RestoreDefaults,
                                         this);
    apply_ = buttons->button(QDialogButtonBox::Apply);
    apply_->setObjectName("preferencesApply");
    reset_ = buttons->button(QDialogButtonBox::RestoreDefaults);
    reset_->setObjectName("preferencesReset");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(apply_, &QPushButton::clicked, this, &PreferencesDialog::apply);
    connect(reset_, &QPushButton::clicked, this, [this] {
        fill(EditorPreferences{});
        ready_ = true;
        status_->clear();
        setBusy(false);
    });
    connect(system_, &QCheckBox::toggled, this, &PreferencesDialog::updatePreview);
    connect(font_, &QFontComboBox::currentFontChanged, this, &PreferencesDialog::updatePreview);
    connect(size_, &QSpinBox::valueChanged, this, &PreferencesDialog::updatePreview);
    connect(adapter, &EngineAdapter::editorPreferencesReady, this,
            [this](quint64 token, const EditorPreferences& value) {
                if (!token_ || token != token_)
                    return;
                token_ = 0;
                const auto validation = shortcutValidationError(value, catalog_);
                if (!validation.isEmpty()) {
                    setBusy(false);
                    status_->setText(validation);
                    return;
                }
                emit preferencesConfirmed(value);
                fill(value);
                ready_ = true;
                setBusy(false);
                status_->setText(saving_ ? tr("Preferences saved.") : tr("Preferences loaded."));
                saving_ = false;
            });
    connect(adapter, &EngineAdapter::recoveryFailed, this,
            [this](quint64 token, const QString& error) {
                if (!token_ || token != token_)
                    return;
                token_ = 0;
                setBusy(false);
                status_->setText(error);
            });
    fill(EditorPreferences{});
    setBusy(true);
    token_ = nextToken();
    adapter_->getEditorPreferences(token_);
}
EditorPreferences PreferencesDialog::draft() const {
    EditorPreferences result;
    result.fontFamily = system_->isChecked() ? QString{} : font_->currentFont().family();
    result.fontSize = size_->value();
    for (qsizetype i = 0; i < catalog_.size(); ++i) {
        if (!catalog_[i].configurable)
            continue;
        const auto selected = sequences_[i]->keySequence();
        const auto normal =
            QKeySequence::fromString(catalog_[i].defaultSequence, QKeySequence::PortableText);
        if (selected != normal)
            result.shortcuts.append(
                {catalog_[i].id, selected.toString(QKeySequence::PortableText)});
    }
    return result;
}
void PreferencesDialog::fill(const EditorPreferences& value) {
    system_->setChecked(value.fontFamily.isEmpty());
    font_->setCurrentFont(value.fontFamily.isEmpty()
                              ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                              : QFont(value.fontFamily));
    size_->setValue(value.fontSize);
    for (qsizetype i = 0; i < catalog_.size(); ++i) {
        QString sequence = catalog_[i].defaultSequence;
        for (const auto& custom : value.shortcuts)
            if (custom.command == catalog_[i].id)
                sequence = custom.sequence;
        sequences_[i]->setKeySequence(
            QKeySequence::fromString(sequence, QKeySequence::PortableText));
    }
    updatePreview();
}
void PreferencesDialog::updatePreview() {
    font_->setEnabled(!system_->isChecked());
    QFont selected = system_->isChecked() ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                                          : font_->currentFont();
    selected.setPointSize(size_->value());
    preview_->setEditorFont(selected);
}
void PreferencesDialog::setBusy(bool busy) {
    busy_ = busy;
    pages_->setEnabled(!busy);
    reset_->setEnabled(!busy);
    apply_->setEnabled(!busy && ready_);
}
void PreferencesDialog::apply() {
    if (busy_ || !ready_ || !adapter_)
        return;
    const auto value = draft();
    const auto error = shortcutValidationError(value, catalog_);
    if (!error.isEmpty()) {
        status_->setText(error);
        return;
    }
    saving_ = true;
    setBusy(true);
    token_ = nextToken();
    status_->setText(tr("Saving preferences…"));
    emit preferencesSaveSubmitted(token_);
    adapter_->setEditorPreferences(value, token_);
}
} // namespace choscordb
