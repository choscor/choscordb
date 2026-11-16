#include "app/connection_tone.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "design_system/menu/menu.h"
#include "design_system/separator/separator.h"
#include <QActionGroup>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QStyle>
#include <algorithm>

namespace choscordb {
void QueryWorkspace::rememberProfile(quint64 connection, const SavedProfile& profile) {
    connectionProfiles_.insert(connection, profile.id);
    connectionSources_.insert(connection, profile);
    connectionDrivers_.insert(connection, profile.driver);
    connectionTags_.insert(connection,
                           {profile.safety, profile.safety, profile.chipLabel, profile.tone});
    connectionRowLimits_.insert(connection, profile.rowLimit);
}

void QueryWorkspace::refreshProfileTag(const SavedProfile& profile) {
    for (auto tag = connectionTags_.begin(); tag != connectionTags_.end(); ++tag)
        if (connectionProfiles_.value(tag.key()) == profile.id) {
            tag->label = profile.chipLabel;
            tag->tone = profile.tone;
        }
    syncConnectionTag();
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
    auto* menu = new QMenu(widgets_.safetyLevel);
    safetyMenu_ = menu;
    menu->setObjectName("safetyLevelMenu");
    menu->setToolTipsVisible(true);
    auto* levels = new QActionGroup(menu);
    for (const auto& level : EngineAdapter::profileTagOptions().safetyLevels) {
        auto* action = menu->addAction(level.label);
        action->setData(level.code);
        action->setCheckable(true);
        levels->addAction(action);
    }
    connect(levels, &QActionGroup::triggered, this, [this](QAction* action) {
        if (const auto connection = selectedConnection())
            connectionTags_[*connection].safety = action->data().toString();
        syncConnectionTag();
    });
    connect(widgets_.safetyLevel, &QPushButton::clicked, menu, [this, menu] {
        design::popupContextMenu(
            *menu, widgets_.safetyLevel->mapToGlobal(widgets_.safetyLevel->rect().bottomLeft()));
    });
    syncConnectionTag();
}

void QueryWorkspace::syncConnectionTag() {
    const auto connection = selectedConnection();
    const auto tag = connection ? connectionTags_.value(*connection) : ConnectionTag{};
    if (widgets_.safetyLevel && safetyMenu_) {
        const auto& options = EngineAdapter::profileTagOptions();
        const auto& unavailable = options.sessionSafetyUnavailable;
        const auto locked = options.sessionLockingSafety.contains(tag.profileSafety);
        widgets_.safetyLevel->setEnabled(connection.has_value() && !locked);
        const auto actions = safetyMenu_->actions();
        for (auto* action : actions) {
            const auto reason = unavailable.value(action->data().toString());
            action->setEnabled(reason.isEmpty());
            action->setToolTip(reason);
        }
        const auto current =
            std::find_if(actions.cbegin(), actions.cend(),
                         [&tag](const QAction* action) { return action->data() == tag.safety; });
        auto* shown = current != actions.cend() ? *current : actions.value(0);
        if (shown) {
            shown->setChecked(true);
            const auto& names = EngineAdapter::profileTagOptions().safetyNames;
            const auto name =
                std::find_if(names.cbegin(), names.cend(), [shown](const CodeLabel& choice) {
                    return choice.code == shown->data().toString();
                });
            widgets_.safetyLevel->setText(name != names.cend() ? name->label : shown->text());
        }
    }
    const auto tone = connectionTone(tag.tone);
    if (widgets_.connectionToneLine)
        design::setToneLine(widgets_.connectionToneLine, connection ? tone : std::nullopt);
    if (!widgets_.connectionTag)
        return;
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
