#include "design_system/dialog_sections/dialog_sections.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
namespace choscordb {
void ProfileDialog::createTagControls() {
    const auto& options = EngineAdapter::profileTagOptions();
    auto combo = [this](const char* object, const QString& name, const QList<CodeLabel>& values) {
        auto* box = new QComboBox(form_);
        box->setObjectName(object);
        box->setAccessibleName(name);
        for (const auto& value : values)
            box->addItem(value.label, value.code);
        connect(box, &QComboBox::currentIndexChanged, this, [this] {
            if (!filling_) {
                dirty_ = true;
                ++revision_;
            }
            updateTagControls();
        });
        sections_->addFormRow(formLayout_, name, box);
        return box;
    };
    environment_ = combo("profileEnvironment", tr("Environment"), options.environments);
    environmentLabel_ = new QLineEdit(form_);
    environmentLabel_->setObjectName("profileEnvironmentLabel");
    environmentLabel_->setAccessibleName(tr("Environment label"));
    environmentLabel_->setMaxLength(options.maxEnvironmentLabelBytes);
    connect(environmentLabel_, &QLineEdit::textEdited, this, [this] {
        dirty_ = true;
        ++revision_;
    });
    environmentLabelRow_ = validated(environmentLabel_);
    sections_->addFormRow(formLayout_, tr("Environment label"), environmentLabelRow_);
    color_ = combo("profileColor", tr("Color"), options.colors);
    safety_ = combo("profileSafety", tr("Safety"), options.safetyLevels);
    updateTagControls();
}
void ProfileDialog::setTagDraft(const SavedProfile& value) {
    environment_->setCurrentIndex(environment_->findData(value.environment));
    environmentLabel_->setText(value.environmentLabel);
    color_->setCurrentIndex(color_->findData(value.color));
    safety_->setCurrentIndex(safety_->findData(value.safety));
    updateTagControls();
}
void ProfileDialog::readTagDraft(SavedProfile& value) const {
    value.environment = environment_->currentData().toString();
    value.environmentLabel =
        value.environment == EngineAdapter::profileTagOptions().labelledEnvironment
            ? environmentLabel_->text()
            : QString();
    value.color = color_->currentData().toString();
    value.safety = safety_->currentData().toString();
}
void ProfileDialog::updateTagControls() {
    const bool labelled = environment_->currentData().toString() ==
                          EngineAdapter::profileTagOptions().labelledEnvironment;
    formLayout_->setRowVisible(environmentLabelRow_, labelled);
}
}
