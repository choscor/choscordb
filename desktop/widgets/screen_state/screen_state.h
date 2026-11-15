#pragma once

#include "bridge/engine_adapter.h"
#include "design_system/panel_state/panel_state.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include "design_system/toast_region/toast_region.h"
#include <QComboBox>

namespace choscordb {

static_assert(int(design::StatusLine::State::Neutral) == int(Tone::Neutral) &&
              int(design::StatusLine::State::Success) == int(Tone::Success) &&
              int(design::StatusLine::State::Error) == int(Tone::Error));

inline constexpr int CsvOptionsRole = Qt::UserRole + 1;
inline constexpr int SqlOptionsRole = Qt::UserRole + 2;

inline QComboBox* codeLabelChoices(const QList<CodeLabel>& values, QWidget* parent) {
    auto* box = new QComboBox(parent);
    for (const auto& value : values)
        box->addItem(value.label, value.code);
    return box;
}

inline QComboBox* transferFormatChoices(const QList<TableExportFormatChoice>& values,
                                        QWidget* parent) {
    auto* box = new QComboBox(parent);
    for (const auto& format : values) {
        box->addItem(format.label, format.code);
        box->setItemData(box->count() - 1, format.csvOptions, CsvOptionsRole);
        box->setItemData(box->count() - 1, format.sqlOptions, SqlOptionsRole);
    }
    return box;
}

inline auto statusLineState(Tone tone) -> design::StatusLine::State {
    return static_cast<design::StatusLine::State>(int(tone));
}

inline auto panelStateKind(PanelBody body) -> design::PanelStateKind {
    switch (body) {
    case PanelBody::Empty:
        return design::PanelStateKind::Empty;
    case PanelBody::NoMatch:
        return design::PanelStateKind::NoMatch;
    case PanelBody::FailedPlaceholder:
        return design::PanelStateKind::Failed;
    case PanelBody::Content:
    case PanelBody::None:
        break;
    }
    return design::PanelStateKind::Hidden;
}

inline void showPresentationToast(QWidget* context, const Presentation& shown) {
    if (!shown.toast)
        return;
    auto* toast = windowToast(context);
    if (!toast)
        return;
    switch (shown.toastTone) {
    case ToastTone::Success:
        toast->showToast(shown.toastTitle, *shown.toast, ToastVariant::Success);
        return;
    case ToastTone::Warning:
        toast->showToast(shown.toastTitle, *shown.toast, ToastVariant::Warning);
        return;
    case ToastTone::Error:
        toast->showToast(shown.toastTitle, *shown.toast, ToastVariant::Danger);
        return;
    }
}

inline void showPanelState(design::Text* label, const Presentation& shown) {
    design::showPanelState(label, panelStateKind(shown.body), shown.message);
}

inline void presentPanel(design::Text* label, PanelKind kind, PanelState state,
                         const QString& detail = {}) {
    const auto shown = EngineAdapter::panelPresentation(kind, state, detail);
    showPanelState(label, shown);
    showPresentationToast(label, shown);
}

inline void presentPanel(design::Text* label, PanelKind kind, const PanelFacts& facts) {
    const auto shown = EngineAdapter::panelObservation(kind, facts).presentation;
    showPanelState(label, shown);
    showPresentationToast(label, shown);
}

}
