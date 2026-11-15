#include "app/connection_tone.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include <QComboBox>
#include <QLabel>
#include <QStyle>

namespace choscordb {
void QueryWorkspace::rememberProfile(quint64 connection, const SavedProfile& profile) {
    connectionProfiles_.insert(connection, profile.id);
    connectionSources_.insert(connection, profile);
    connectionDrivers_.insert(connection, profile.driver);
    connectionTags_.insert(connection,
                           {profile.safety, profile.safety, profile.tagLabel, profile.tone});
}

std::optional<SavedProfile> QueryWorkspace::connectionSource(quint64 connection) const {
    const auto source = connectionSources_.constFind(connection);
    if (source == connectionSources_.cend())
        return std::nullopt;
    return *source;
}

void QueryWorkspace::setupSafetyLevel() {
    if (!widgets_.safetyLevel)
        return;
    for (const auto& level : EngineAdapter::profileTagOptions().safetyLevels)
        widgets_.safetyLevel->addItem(level.label, level.code);
    connect(widgets_.safetyLevel, &QComboBox::activated, this, [this](int index) {
        if (const auto connection = selectedConnection())
            connectionTags_[*connection].safety = widgets_.safetyLevel->itemData(index).toString();
    });
    syncConnectionTag();
}

void QueryWorkspace::syncConnectionTag() {
    const auto connection = selectedConnection();
    const auto tag = connection ? connectionTags_.value(*connection) : ConnectionTag{};
    if (widgets_.safetyLevel) {
        const auto locked =
            tag.profileSafety == EngineAdapter::profileTagOptions().sessionLockedSafety;
        widgets_.safetyLevel->setEnabled(connection.has_value() && !locked);
        const auto index = widgets_.safetyLevel->findData(tag.safety);
        widgets_.safetyLevel->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (!widgets_.connectionTag)
        return;
    const auto tone = connectionTone(tag.tone);
    widgets_.connectionTag->setVisible(connection.has_value() && !tag.label.isEmpty());
    widgets_.connectionTag->setText(tag.label);
    const QString variant =
        tone ? QString::fromLatin1(design::statusToneBadgeVariant(*tone)) : QString();
    if (widgets_.connectionTag->property("variant").toString() != variant) {
        widgets_.connectionTag->setProperty("variant", variant);
        widgets_.connectionTag->style()->unpolish(widgets_.connectionTag);
        widgets_.connectionTag->style()->polish(widgets_.connectionTag);
    }
}
}
