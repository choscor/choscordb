#include "preferences_dialog.h"
#include "app/appearance_controller.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/field/field.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include "design_system/toast_region/toast_region.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QVBoxLayout>
#include <atomic>
#include <limits>
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
    setAppModal();
    setWindowTitle(tr("Preferences"));
    setObjectName("preferencesDialog");
    resize(design::dialogInitialSize(design::DialogSize::Preferences));
    const auto metrics = design::resolveMetrics(design::Density::Compact, true);
    auto* layout = new QVBoxLayout(this);
    auto* sections = new design::DialogSections(this);
    layout->addWidget(sections);
    auto* headerLayout = sections->headerLayout();
    auto* header = headerLayout->parentWidget();
    header->setFixedHeight(metrics.modalHeaderHeight);
    headerLayout->setContentsMargins(metrics.modalContentInset, 0, metrics.modalContentInset, 0);
    auto* heading = new design::Text(tr("Preferences"), header);
    heading->setTypographyRole(design::TypographyRole::DialogTitle);
    headerLayout->addWidget(heading);
    headerLayout->addStretch();
    auto* dismiss = new design::Button({}, header);
    dismiss->setObjectName("preferencesDismiss");
    dismiss->setAccessibleName(tr("Close preferences"));
    dismiss->setVariant(design::ButtonVariant::Ghost);
    dismiss->setButtonSize(design::ButtonSize::IconSmall);
    dismiss->setDesignIcon(design::Icon::Close);
    headerLayout->addWidget(dismiss);
    connect(dismiss, &QPushButton::clicked, this, &PreferencesDialog::reject);
    auto* pages = new QTabWidget(this);
    pages->setObjectName("preferencesSections");
    pages->setAccessibleName(tr("Preference sections"));
    pages->setDocumentMode(true);
    pages->setUsesScrollButtons(true);
    pages_ = pages;
    auto* body = sections->bodyLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(pages, 1);
    auto addPage = [pages, metrics](QWidget* page, const QString& title) {
        auto* scroll = new QScrollArea(pages);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->viewport()->setProperty("designSurface", "panel");
        page->setProperty("designSurface", "panel");
        page->setAttribute(Qt::WA_StyledBackground);
        page->setBackgroundRole(QPalette::Base);
        if (page->layout())
            page->layout()->setContentsMargins(metrics.modalContentInset, metrics.modalFooterInset,
                                               metrics.modalContentInset, metrics.modalFooterInset);
        scroll->setWidget(page);
        pages->addTab(scroll, title);
    };

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
    fontValidation_ = new design::FieldValidation(font_, editor);
    sizeValidation_ = new design::FieldValidation(size_, editor);
    form->addRow(tr("Font family"), fontValidation_);
    form->addRow(tr("Font size"), sizeValidation_);
    connect(font_, &QFontComboBox::currentFontChanged, fontValidation_,
            [this] { fontValidation_->setError({}); });
    connect(size_, &QSpinBox::valueChanged, sizeValidation_,
            [this] { sizeValidation_->setError({}); });
    preview_ = new SqlEditor(editor);
    preview_->setObjectName("preferencesPreview");
    preview_->setText("SELECT name, count(*)\nFROM sample\nWHERE active = true\nGROUP BY name;");
    preview_->setReadOnly(true);
    form->addRow(preview_);
    addPage(editor, tr("SQL editor"));
    auto* results = new QWidget(pages);
    auto* resultForm = new QFormLayout(results);
    const auto queryLimits = EngineAdapter::queryPreferenceLimits();
    pageSize_ = new QSpinBox(results);
    pageSize_->setObjectName("queryPageSize");
    pageSize_->setRange(queryLimits.minPageSize, queryLimits.maxPageSize);
    timeout_ = new QSpinBox(results);
    timeout_->setObjectName("queryTimeoutSeconds");
    timeout_->setRange(0, queryLimits.maxTimeoutSeconds);
    timeout_->setSpecialValueText(tr("No timeout"));
    resultForm->addRow(tr("Rows per page"), pageSize_);
    resultForm->addRow(tr("Statement timeout (seconds)"), timeout_);
    resultForm->addRow(createDescription(
        tr("Applies to new queries. Existing results keep their page size and timeout."), results));
    addPage(results, tr("Results && execution"));
    auto* history = new QWidget(pages);
    auto* historyForm = new QFormLayout(history);
    recordHistory_ = new QCheckBox(tr("Record query history"), history);
    recordHistory_->setObjectName("preferencesRecordHistory");
    historyDays_ = new QDoubleSpinBox(history);
    historyDays_->setDecimals(0);
    historyDays_->setObjectName("preferencesHistoryDays");
    historyDays_->setRange(1, std::numeric_limits<quint32>::max());
    historyRecords_ = new QDoubleSpinBox(history);
    historyRecords_->setDecimals(0);
    historyRecords_->setObjectName("preferencesHistoryRecords");
    historyRecords_->setRange(1, std::numeric_limits<quint32>::max());
    historyForm->addRow(recordHistory_);
    historyForm->addRow(tr("Keep history (days)"), historyDays_);
    historyForm->addRow(tr("Maximum history entries"), historyRecords_);
    historyForm->addRow(createDescription(
        tr("Unsaved SQL drafts are recovered automatically. Restored drafts stay disconnected "
           "and never execute automatically."),
        history));
    addPage(history, tr("History && recovery"));
    auto* keyboard = new QWidget(pages);
    auto* keys = new QFormLayout(keyboard);
    for (const auto& descriptor : catalog_) {
        auto* key = new QKeySequenceEdit(keyboard);
        key->setObjectName("shortcut_" + descriptor.id);
        key->setEnabled(descriptor.configurable);
        sequences_.append(key);
        auto* validation = new design::FieldValidation(key, keyboard);
        sequenceValidations_.append(validation);
        keys->addRow(descriptor.label, validation);
        connect(key, &QKeySequenceEdit::keySequenceChanged, validation,
                [validation] { validation->setError({}); });
    }
    addPage(keyboard, tr("Keyboard shortcuts"));
    pages->setCurrentIndex(0);
    status_ = createInlineStatus(this);
    status_->setObjectName("preferencesStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    body->addWidget(status_);
    auto* buttons = sections->footerLayout();
    auto* footer = buttons->parentWidget();
    footer->setProperty("designSurface", "muted");
    footer->setAttribute(Qt::WA_StyledBackground);
    buttons->setContentsMargins(metrics.modalFooterInset, metrics.modalFooterVerticalInset,
                                metrics.modalFooterInset, metrics.modalFooterVerticalInset);
    auto* reset = new design::Button(tr("Restore defaults"), footer);
    reset_ = reset;
    reset->setObjectName("preferencesReset");
    reset->setVariant(design::ButtonVariant::Outline);
    reset->setButtonSize(design::ButtonSize::Small);
    buttons->addWidget(reset);
    buttons->addStretch();
    auto* close = new design::Button(tr("Close"), footer);
    close->setObjectName("preferencesClose");
    close->setVariant(design::ButtonVariant::Outline);
    close->setButtonSize(design::ButtonSize::Small);
    buttons->addWidget(close);
    auto* apply = new design::Button(tr("Save preferences"), footer);
    apply_ = apply;
    apply->setObjectName("preferencesApply");
    apply->setButtonSize(design::ButtonSize::Small);
    buttons->addWidget(apply);
    connect(close, &QPushButton::clicked, this, &PreferencesDialog::reject);
    connect(this, &QDialog::rejected, this, [this] {
        if (appearance_)
            appearance_->cancelPreview();
    });
    connect(apply_, &QPushButton::clicked, this, &PreferencesDialog::apply);
    connect(reset_, &QPushButton::clicked, this, [this] {
        fill(EditorPreferences{});
        fillQuery(QueryPreferences{});
        fillHistory(HistoryPolicy{});
        errors_.clear();
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
                    if (!appearancePending_)
                        return;
                    appearancePending_ = false;
                    if (!success)
                        errors_.append(tr("Appearance: %1").arg(message));
                    finishRequests();
                });
        connect(retryAppearance, &QPushButton::clicked, appearance_, &AppearanceController::retry);
        connect(resetAppearance, &QPushButton::clicked, appearance_,
                &AppearanceController::stageReset);
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
                if (!validation.isEmpty())
                    errors_.append(tr("SQL editor / Keyboard shortcuts: %1").arg(validation));
                else {
                    emit preferencesConfirmed(value);
                    if (!saving_)
                        fill(value);
                }
                finishRequests();
            });
    connect(adapter, &EngineAdapter::queryPreferencesReady, this,
            [this](quint64 token, const QueryPreferences& value) {
                if (!queryToken_ || token != queryToken_)
                    return;
                queryToken_ = 0;
                const auto limits = EngineAdapter::queryPreferenceLimits();
                if (value.version != limits.version || value.pageSize < limits.minPageSize ||
                    value.pageSize > limits.maxPageSize ||
                    value.timeoutSeconds > limits.maxTimeoutSeconds)
                    errors_.append(tr("Results & execution: stored settings are invalid. "
                                      "Restore defaults to replace them."));
                else {
                    if (!saving_)
                        fillQuery(value);
                    emit queryPreferencesConfirmed(value);
                }
                finishRequests();
            });
    connect(adapter, &EngineAdapter::historyPolicyReady, this,
            [this](quint64 token, const HistoryPolicy& value) {
                if (!historyToken_ || token != historyToken_)
                    return;
                historyToken_ = 0;
                if (!value.maxAgeDays || !value.maxRecords ||
                    value.maxAgeDays > quint32(historyDays_->maximum()) ||
                    value.maxRecords > quint32(historyRecords_->maximum()))
                    errors_.append(tr("History & recovery: stored retention is outside the "
                                      "supported range. Restore defaults to replace it."));
                else {
                    if (!saving_)
                        fillHistory(value);
                    emit historyPolicyConfirmed(value);
                }
                finishRequests();
            });
    connect(adapter, &EngineAdapter::recoveryFailed, this,
            [this](quint64 token, const QString& error) {
                if (!token)
                    return;
                QString section;
                if (token == token_) {
                    token_ = 0;
                    section = tr("SQL editor / Keyboard shortcuts");
                } else if (token == queryToken_) {
                    queryToken_ = 0;
                    section = tr("Results & execution");
                } else if (token == historyToken_) {
                    historyToken_ = 0;
                    section = tr("History & recovery");
                } else
                    return;
                errors_.append(tr("%1: %2").arg(section, error));
                finishRequests();
            });
    fill(EditorPreferences{});
    fillQuery(QueryPreferences{});
    fillHistory(HistoryPolicy{});
    setBusy(true);
    // The tabs are disabled during asynchronous loading. Give the modal an
    // enabled initial focus target instead of leaving Cocoa's focus empty.
    close->setFocus(Qt::OtherFocusReason);
    token_ = nextToken();
    queryToken_ = nextToken();
    historyToken_ = nextToken();
    if (adapter_) {
        adapter_->getEditorPreferences(token_);
        adapter_->getQueryPreferences(queryToken_);
        adapter_->getHistoryPolicy(historyToken_);
    } else {
        token_ = queryToken_ = historyToken_ = 0;
        errors_.append(tr("Settings service is unavailable."));
        finishRequests();
    }
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
    if (busy)
        progressToast(this)->showProgress(tr("Preferences"), tr("Saving or loading preferences…"));
    else
        clearProgressToast(this);
    if (busy)
        status_->clear();
    busy_ = busy;
    pages_->setEnabled(!busy);
    reset_->setEnabled(!busy);
    apply_->setEnabled(!busy && ready_ && appearanceValid_);
}
bool PreferencesDialog::placeValidationError(const QString& message) {
    fontValidation_->setError({});
    sizeValidation_->setError({});
    for (auto* validation : sequenceValidations_)
        validation->setError({});
    auto* tabs = static_cast<QTabWidget*>(pages_);
    if (message.startsWith(tr("Font size"))) {
        tabs->setCurrentIndex(1);
        sizeValidation_->setError(message);
        size_->setFocus();
        return true;
    }
    if (message.startsWith(tr("Font family"))) {
        tabs->setCurrentIndex(1);
        fontValidation_->setError(message);
        font_->setFocus();
        return true;
    }
    for (qsizetype index = 0; index < catalog_.size(); ++index) {
        if (message.contains(catalog_[index].label)) {
            tabs->setCurrentIndex(4);
            sequenceValidations_[index]->setError(message);
            sequences_[index]->setFocus();
            return true;
        }
    }
    return false;
}
void PreferencesDialog::apply() {
    if (busy_ || !ready_ || !appearanceValid_ || !adapter_)
        return;
    const auto value = draft();
    const auto error = shortcutValidationError(value, catalog_);
    if (!error.isEmpty()) {
        if (!placeValidationError(error))
            status_->setText(error);
        return;
    }
    QueryPreferences query;
    query.pageSize = quint32(pageSize_->value());
    query.timeoutSeconds = quint32(timeout_->value());
    HistoryPolicy history;
    history.enabled = recordHistory_->isChecked();
    history.maxAgeDays = quint32(historyDays_->value());
    history.maxRecords = quint32(historyRecords_->value());
    errors_.clear();
    saving_ = true;
    setBusy(true);
    // Register every participant before submitting: shutdown/queue failures may
    // be synchronous, while success must wait for all storage acknowledgements.
    const auto editorToken = token_ = nextToken();
    const auto queryToken = queryToken_ = nextToken();
    const auto historyToken = historyToken_ = nextToken();
    appearancePending_ = !appearance_.isNull();
    progressToast(this)->showProgress(tr("Preferences"), tr("Saving preferences…"));
    emit preferencesSaveSubmitted(editorToken);
    emit queryPreferencesSaveSubmitted(queryToken);
    adapter_->setEditorPreferences(value, editorToken);
    adapter_->setQueryPreferences(query, queryToken);
    adapter_->setHistoryPolicy(history, historyToken);
    if (appearance_)
        appearance_->applyPreview();
}
void PreferencesDialog::fillQuery(const QueryPreferences& value) {
    pageSize_->setValue(value.pageSize);
    timeout_->setValue(value.timeoutSeconds);
}
void PreferencesDialog::fillHistory(const HistoryPolicy& value) {
    recordHistory_->setChecked(value.enabled);
    historyDays_->setValue(value.maxAgeDays);
    historyRecords_->setValue(value.maxRecords);
}
void PreferencesDialog::finishRequests() {
    if (token_ || queryToken_ || historyToken_ || appearancePending_)
        return;
    const bool saved = saving_;
    saving_ = false;
    if (!saved)
        ready_ = errors_.isEmpty();
    setBusy(false);
    if (!errors_.isEmpty()) {
        status_->setText(errors_.join(QLatin1Char('\n')));
        return;
    }
    status_->setText(saved ? tr("Preferences saved.") : tr("Preferences loaded."));
    if (saved)
        accept();
}
void PreferencesDialog::reject() {
    if (saving_) {
        progressToast(this)->showProgress(
            tr("Preferences"), tr("Saving preferences… Please wait for storage to finish."));
        return;
    }
    DialogShell::reject();
}
void PreferencesDialog::showEvent(QShowEvent* event) {
    DialogShell::showEvent(event);
    layout()->setContentsMargins(0, 0, 0, 0);
    layout()->setSpacing(0);
}
void PreferencesDialog::closeEvent(QCloseEvent* event) {
    if (saving_) {
        progressToast(this)->showProgress(
            tr("Preferences"), tr("Saving preferences… Please wait for storage to finish."));
        event->ignore();
        return;
    }
    DialogShell::closeEvent(event);
}
} // namespace choscordb
