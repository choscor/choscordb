#include "preferences_dialog.h"
#include "app/appearance_controller.h"
#include "design_system/button/button.h"
#include "design_system/theme.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
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
                                     QWidget* parent, AppearanceController* appearance)
    : DialogShell(parent), adapter_(adapter), appearance_(appearance),
      catalog_(std::move(catalog)) {
    setWindowTitle(tr("Preferences"));
    setObjectName("preferencesDialog");
    resize(design::dialogInitialSize(design::DialogSize::Preferences));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(createDescription(
        tr("Tune appearance, editor behavior, and keyboard shortcuts. Appearance changes preview "
           "immediately."),
        this));
    auto* pageRow = new QHBoxLayout;
    auto* navigation = new QListWidget(this);
    navigation->setObjectName("preferencesSections");
    navigation->setAccessibleName(tr("Preference sections"));
    auto* pages = new QStackedWidget(this);
    pages_ = pages;
    pageRow->addWidget(navigation);
    pageRow->addWidget(pages, 1);
    layout->addLayout(pageRow, 1);
    auto addPage = [navigation, pages](QWidget* page, const QString& title) {
        navigation->addItem(title);
        pages->addWidget(page);
    };
    connect(navigation, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);

    auto* appearancePage = new QWidget(pages);
    auto* appearanceForm = new QFormLayout(appearancePage);
    theme_ = new QComboBox(appearancePage);
    theme_->setObjectName("appearanceTheme");
    theme_->setAccessibleName(tr("Theme"));
    theme_->addItem(tr("System"), "system");
    theme_->addItem(tr("Light"), "light");
    theme_->addItem(tr("Dark"), "dark");
    appearanceStatus_ = createInlineStatus(appearancePage);
    appearanceStatus_->setObjectName("appearanceStatus");
    appearanceStatus_->setTextFormat(Qt::PlainText);
    appearanceStatus_->setWordWrap(true);
    appearanceStatus_->hide();
    appearanceForm->addRow(tr("Theme"), theme_);
    appearanceForm->addRow(appearanceStatus_);
    auto* appearanceActions = new QWidget(appearancePage);
    auto* appearanceActionsLayout = new QHBoxLayout(appearanceActions);
    appearanceActionsLayout->setContentsMargins(0, 0, 0, 0);
    auto* retryAppearance = new design::Button(tr("Retry load"), appearanceActions);
    retryAppearance->setVariant(design::ButtonVariant::Outline);
    retryAppearance->setObjectName("appearanceRetry");
    auto* resetAppearance =
        new design::Button(tr("Reset appearance and layout"), appearanceActions);
    resetAppearance->setVariant(design::ButtonVariant::Destructive);
    resetAppearance->setObjectName("appearanceReset");
    appearanceActionsLayout->addWidget(retryAppearance);
    appearanceActionsLayout->addWidget(resetAppearance);
    appearanceActionsLayout->addStretch();
    appearanceForm->addRow(appearanceActions);
    addPage(appearancePage, tr("Appearance"));

    auto* editor = new QWidget(pages);
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
    addPage(editor, tr("Editor"));
    auto* scroll = new QScrollArea(pages);
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
    addPage(scroll, tr("Keyboard"));
    navigation->setCurrentRow(0);
    status_ = createInlineStatus(this);
    status_->setObjectName("preferencesStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel |
                                             QDialogButtonBox::RestoreDefaults,
                                         this);
    apply_ = buttons->button(QDialogButtonBox::Apply);
    apply_->setObjectName("preferencesApply");
    apply_->setProperty("variant", "default");
    buttons->button(QDialogButtonBox::Cancel)->setProperty("variant", "outline");
    reset_ = buttons->button(QDialogButtonBox::RestoreDefaults);
    reset_->setObjectName("preferencesReset");
    reset_->setProperty("variant", "outline");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::rejected, this, [this] {
        if (appearance_)
            appearance_->cancelPreview();
    });
    connect(apply_, &QPushButton::clicked, this, &PreferencesDialog::apply);
    connect(reset_, &QPushButton::clicked, this, [this] {
        fill(EditorPreferences{});
        if (appearance_) {
            theme_->setCurrentIndex(theme_->findData("system"));
        }
        ready_ = true;
        status_->clear();
        setBusy(false);
    });
    connect(system_, &QCheckBox::toggled, this, &PreferencesDialog::updatePreview);
    connect(font_, &QFontComboBox::currentFontChanged, this, &PreferencesDialog::updatePreview);
    connect(size_, &QSpinBox::valueChanged, this, &PreferencesDialog::updatePreview);
    const auto appearanceChanged = [this] {
        if (appearance_)
            appearanceValid_ = appearance_->preview(theme_->currentData().toString());
        apply_->setEnabled(!busy_ && ready_ && appearanceValid_);
    };
    connect(theme_, &QComboBox::currentIndexChanged, this, appearanceChanged);
    if (appearance_) {
        appearanceWarning_ = appearance_->currentWarning();
        forcedContrast_ = appearance_->forcedContrast();
        updateAppearanceStatus();
        const auto showAppearance = [this](const AppearanceLayout& value) {
            const QSignalBlocker themeBlocker(theme_);
            theme_->setCurrentIndex(qMax(0, theme_->findData(value.theme)));
        };
        showAppearance(appearance_->current());
        connect(appearance_, &AppearanceController::resolvedChoicesChanged, this, showAppearance);
        connect(appearance_, &AppearanceController::readyChanged, this, [this](bool ready) {
            if (ready) {
                appearanceValid_ = appearance_->canSave();
                apply_->setEnabled(!busy_ && ready_ && appearanceValid_);
            }
        });
        connect(appearance_, &AppearanceController::warningChanged, this,
                [this](const QString& warning) {
                    appearanceWarning_ = warning;
                    updateAppearanceStatus();
                });
        connect(appearance_, &AppearanceController::accessibilityPolicyChanged, this,
                [this](bool forcedContrast, bool) {
                    forcedContrast_ = forcedContrast;
                    updateAppearanceStatus();
                });
        connect(appearance_, &AppearanceController::saveFinished, this,
                [this](bool success, const QString& message) {
                    if (!success) {
                        appearanceWarning_ = message;
                        updateAppearanceStatus();
                    }
                });
        connect(retryAppearance, &QPushButton::clicked, appearance_, &AppearanceController::retry);
        connect(resetAppearance, &QPushButton::clicked, appearance_, &AppearanceController::reset);
    } else {
        retryAppearance->setEnabled(false);
        resetAppearance->setEnabled(false);
    }
    appearanceChanged();
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

void PreferencesDialog::updateAppearanceStatus() {
    QStringList messages;
    if (!appearanceWarning_.isEmpty()) {
        messages.append(appearanceWarning_);
    }
    if (forcedContrast_) {
        messages.append(tr("System high-contrast colors currently override theme "
                           "colors. Your choices are retained."));
    }
    appearanceStatus_->setText(messages.join(QLatin1Char('\n')));
    appearanceStatus_->setVisible(!messages.isEmpty());
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
                              ? design::resolveTypography(design::TypographyRole::Monospace)
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
    QFont selected = system_->isChecked()
                         ? design::resolveTypography(design::TypographyRole::Monospace)
                         : font_->currentFont();
    selected.setPointSize(size_->value());
    preview_->setEditorFont(selected);
}
void PreferencesDialog::setBusy(bool busy) {
    busy_ = busy;
    pages_->setEnabled(!busy);
    reset_->setEnabled(!busy);
    apply_->setEnabled(!busy && ready_ && appearanceValid_);
}
void PreferencesDialog::apply() {
    if (busy_ || !ready_ || !appearanceValid_ || !adapter_)
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
    if (appearance_)
        appearance_->applyPreview();
}
} // namespace choscordb
